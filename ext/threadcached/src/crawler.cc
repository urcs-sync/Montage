/*  Copyright 2016 Netflix.
 *
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 */

/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "memcached.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <poll.h>

#include "bipbuffer.h"
#define LARGEST_ID POWER_LARGEST

struct crawler_module_reg_t;
struct crawler_module_t;

typedef void (*crawler_eval_func)(crawler_module_t *cm, item *it, uint32_t hv, int slab_cls);
typedef int (*crawler_init_func)(crawler_module_t *cm, void *data); // TODO: init args?
typedef void (*crawler_deinit_func)(crawler_module_t *cm); // TODO: extra args?
typedef void (*crawler_doneclass_func)(crawler_module_t *cm, int slab_cls);
typedef void (*crawler_finalize_func)(crawler_module_t *cm);

struct crawler_module_reg_t {
  crawler_init_func init; /* run before crawl starts */
  crawler_eval_func eval; /* runs on an item. */
  crawler_doneclass_func doneclass; /* runs once per sub-crawler completion. */
  crawler_finalize_func finalize; /* runs once when all sub-crawlers are done. */
  bool needs_lock; /* whether or not we need the LRU lock held when eval is called */
};

struct crawler_module_t {
  pptr<crawler_expired_data> data = nullptr; /* opaque data pointer */
  pptr<crawler_module_reg_t> mod = nullptr;
};

static int crawler_expired_init(crawler_module_t *cm, void *data);
static void crawler_expired_doneclass(crawler_module_t *cm, int slab_cls);
static void crawler_expired_finalize(crawler_module_t *cm);
static void crawler_expired_eval(crawler_module_t *cm, item *search, uint32_t hv, int i);

crawler_module_reg_t crawler_expired_mod = {
  .init = crawler_expired_init,
  .eval = crawler_expired_eval,
  .doneclass = crawler_expired_doneclass,
  .finalize = crawler_expired_finalize,
  .needs_lock = true,
};

static void crawler_metadump_eval(crawler_module_t *cm, item *search, uint32_t hv, int i);

crawler_module_reg_t crawler_metadump_mod = {
  .init = NULL,
  .eval = crawler_metadump_eval,
  .doneclass = NULL,
  .needs_lock = false,
};

crawler_module_reg_t *crawler_mod_regs[3] = {
  &crawler_expired_mod,
  &crawler_expired_mod,
  &crawler_metadump_mod
};

crawler_module_t active_crawler_mod;
enum crawler_run_type active_crawler_type;

static crawler crawlers[LARGEST_ID];

static int crawler_count = 0;
static volatile int do_run_lru_crawler_thread = 0;
static int lru_crawler_initialized = 0;
static pthread_mutex_t lru_crawler_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  lru_crawler_cond = PTHREAD_COND_INITIALIZER;

/* Will crawl all slab classes a minimum of once per hour */

//static const int MAX_MAINTCRAWL_WAIT = 60 * 60;

/*** LRU CRAWLER THREAD ***/

//static const int LRU_CRAWLER_WRITEBUF = 8192;

static int crawler_expired_init(crawler_module_t *cm, void *data) {
  crawler_expired_data *d;
  if (data != NULL) {
    d = (crawler_expired_data*)data;
    d->is_external = true;
    cm->data = (crawler_expired_data*)data;
  } else {
    // allocate data.
    d = (crawler_expired_data*)pm_malloc(sizeof(crawler_expired_data));
    if (d == NULL) {
      return -1;
    }
    // init lock.
    pthread_mutex_init(&d->lock, NULL);
    d->is_external = false;
    d->start_time = *current_time;

    cm->data = d;
  }
  pthread_mutex_lock(&d->lock);
  memset(&d->crawlerstats, 0, sizeof(crawlerstats_t) * POWER_LARGEST);
  for (int x = 0; x < POWER_LARGEST; x++) {
    d->crawlerstats[x].start_time = *current_time;
    d->crawlerstats[x].run_complete = false;
  }
  pthread_mutex_unlock(&d->lock);
  return 0;
}

static void crawler_expired_doneclass(crawler_module_t *cm, int slab_cls) {
  struct crawler_expired_data *d = (struct crawler_expired_data *) cm->data;
  pthread_mutex_lock(&d->lock);
  d->crawlerstats[slab_cls].end_time = *current_time;
  d->crawlerstats[slab_cls].run_complete = true;
  pthread_mutex_unlock(&d->lock);
}

static void crawler_expired_finalize(crawler_module_t *cm) {
  struct crawler_expired_data *d = (struct crawler_expired_data *) cm->data;
  pthread_mutex_lock(&d->lock);
  d->end_time = *current_time;
  d->crawl_complete = true;
  pthread_mutex_unlock(&d->lock);

  if (!d->is_external) {
    free(d);
  }
}

/* I pulled this out to make the main thread clearer, but it reaches into the
 * main thread's values too much. Should rethink again.
 */
static void crawler_expired_eval(crawler_module_t *cm, item *search, uint32_t hv, int i) {
  struct crawler_expired_data *d = (struct crawler_expired_data *) cm->data;
  pthread_mutex_lock(&d->lock);
  crawlerstats_t *s = &d->crawlerstats[i];
  int is_flushed = item_is_flushed(search);
  if ((search->exptime != 0 && search->exptime < *current_time) || is_flushed) {
    crawlers[i].reclaimed++;
    s->reclaimed++;
    // LRU crawler found an expired item
    if ((search->it_flags & ITEM_FETCHED) == 0 && !is_flushed) {
      crawlers[i].unfetched++;
    }
    do_item_unlink_nolock(search, hv);
    do_item_remove(search);
  } else {
    s->seen++;
    refcount_decr(search);
    if (search->exptime == 0) {
      s->noexp++;
    } else if (search->exptime - *current_time > 3599) {
      s->ttl_hourplus++;
    } else {
      rel_time_t ttl_remain = search->exptime - *current_time;
      int bucket = ttl_remain / 60;
      if (bucket <= 60) {
        s->histo[bucket]++;
      }
    }
  }
  pthread_mutex_unlock(&d->lock);
}

static void crawler_metadump_eval(crawler_module_t *cm, item *it, uint32_t hv, int i) {
  refcount_decr(it);
}

static pthread_mutex_t lru_running = PTHREAD_MUTEX_INITIALIZER;

static void lru_crawler_class_done(int i) {
  crawlers[i].it_flags = 0;
  crawler_count--;
  do_item_unlinktail_q((item *)&crawlers[i]);
  do_item_stats_add_crawl(i, crawlers[i].reclaimed,
      crawlers[i].unfetched, crawlers[i].checked);
  pthread_mutex_unlock(&lru_locks[i]);
  if (active_crawler_mod.mod->doneclass != NULL)
    active_crawler_mod.mod->doneclass(&active_crawler_mod, i);
}

static void *item_crawler_thread(void *arg) {
  int i;
  int crawls_persleep = settings.crawls_persleep;

  pthread_mutex_lock(&lru_crawler_lock);
  pthread_cond_signal(&lru_crawler_cond);
  settings.lru_crawler = true;
  // Starting LRU crawler background thread
  while (do_run_lru_crawler_thread) {
    pthread_cond_wait(&lru_crawler_cond, &lru_crawler_lock);

    while (crawler_count) {
      item *search = NULL;
      void *hold_lock = NULL;

      for (i = POWER_SMALLEST; i < LARGEST_ID; i++) {
        if (crawlers[i].it_flags != 1) {
          continue;
        }

        pthread_mutex_lock(&lru_locks[i]);
        search = do_item_crawl_q((item *)&crawlers[i]);
        if (search == NULL ||
            (crawlers[i].remaining && --crawlers[i].remaining < 1)) {
          // Nothing left to crawl for i
          lru_crawler_class_done(i);
          continue;
        }
        uint32_t hv = tcd_hash(ITEM_key(search), search->nkey);
        /* Attempt to hash item lock the "search" item. If locked, no
         * other callers can incr the refcount
         */
        if ((hold_lock = item_trylock(hv)) == NULL) {
          pthread_mutex_unlock(&lru_locks[i]);
          continue;
        }
        /* Now see if the item is refcount locked */
        if (refcount_incr(search) != 2) {
          refcount_decr(search);
          if (hold_lock)
            item_trylock_unlock(hold_lock);
          pthread_mutex_unlock(&lru_locks[i]);
          continue;
        }

        crawlers[i].checked++;
        /* Frees the item or decrements the refcount. */
        /* Interface for this could improve: do the free/decr here
         * instead? */
        if (!active_crawler_mod.mod->needs_lock) {
          pthread_mutex_unlock(&lru_locks[i]);
        }

        active_crawler_mod.mod->eval(&active_crawler_mod, search, hv, i);

        if (hold_lock)
          item_trylock_unlock(hold_lock);
        if (active_crawler_mod.mod->needs_lock) {
          pthread_mutex_unlock(&lru_locks[i]);
        }

        if (crawls_persleep-- <= 0 && settings.lru_crawler_sleep) {
          pthread_mutex_unlock(&lru_crawler_lock);
          usleep(settings.lru_crawler_sleep);
          pthread_mutex_lock(&lru_crawler_lock);
          crawls_persleep = settings.crawls_persleep;
        } else if (!settings.lru_crawler_sleep) {
          // TODO: only cycle lock every N?
          pthread_mutex_unlock(&lru_crawler_lock);
          pthread_mutex_lock(&lru_crawler_lock);
        }
      }
    }

    pthread_mutex_lock(&lru_running);
    if (active_crawler_mod.mod != NULL) {
      if (active_crawler_mod.mod->finalize != NULL)
        active_crawler_mod.mod->finalize(&active_crawler_mod);
      // TODO check that it's not running
      // TODO fix this
      //active_crawler_mod.mod = NULL;
    }
    pthread_mutex_unlock(&lru_running);
    // LRU crawler thread sleeping
  }
  STATS_LOCK();
  __global_stats->lru_crawler_running = false;
  STATS_UNLOCK();
  pthread_mutex_unlock(&lru_crawler_lock);
  // LRU crawler thread stopping
  return NULL;
}

static pthread_t item_crawler_tid;

int stop_item_crawler_thread(void) {
  int ret;
  pthread_mutex_lock(&lru_crawler_lock);
  do_run_lru_crawler_thread = 0;
  pthread_cond_signal(&lru_crawler_cond);
  pthread_mutex_unlock(&lru_crawler_lock);
  if ((ret = pthread_join(item_crawler_tid, NULL)) != 0) {
    fprintf(stderr, "Failed to stop LRU crawler thread: %s\n", strerror(ret));
    return -1;
  }
  settings.lru_crawler = false;
  return 0;
}

/* Lock dance to "block" until thread is waiting on its condition:
 * caller locks mtx. caller spawns thread.
 * thread blocks on mutex.
 * caller waits on condition, releases lock.
 * thread gets lock, sends signal.
 * caller can't wait, as thread has lock.
 * thread waits on condition, releases lock
 * caller wakes on condition, gets lock.
 * caller immediately releases lock.
 * thread is now safely waiting on condition before the caller returns.
 */
int start_item_crawler_thread(void) {
  int ret;

  if (settings.lru_crawler)
    return -1;
  pthread_mutex_lock(&lru_crawler_lock);
  do_run_lru_crawler_thread = 1;
  if ((ret = pthread_create(&item_crawler_tid, NULL,
          item_crawler_thread, NULL)) != 0) {
    fprintf(stderr, "Can't create LRU crawler thread: %s\n",
        strerror(ret));
    pthread_mutex_unlock(&lru_crawler_lock);
    return -1;
  }
  /* Avoid returning until the crawler has actually started */
  pthread_cond_wait(&lru_crawler_cond, &lru_crawler_lock);
  pthread_mutex_unlock(&lru_crawler_lock);

  return 0;
}

/* 'remaining' is passed in so the LRU maintainer thread can scrub the whole
 * LRU every time.
 */
static int do_lru_crawler_start(uint32_t id, uint32_t remaining) {
  uint32_t sid = id;
  int starts = 0;
//  bool is_running;

  pthread_mutex_lock(&lru_locks[sid]);
//  STATS_LOCK();
//  is_running = __global_stats->lru_crawler_running;
//  STATS_UNLOCK();

  // TODO - fix crawler
  if (crawlers[sid].it_flags == 0) {
    // Kicking LRU crawler off for LRU 'sid'
    crawlers[sid].nbytes = 0;
    crawlers[sid].nkey = 0;
    crawlers[sid].it_flags = 1; /* For a crawler, this means enabled. */
    crawlers[sid].next = 0;
    crawlers[sid].prev = 0;
    crawlers[sid].time = 0;
    if ((int)remaining == LRU_CRAWLER_CAP_REMAINING) {
      remaining = do_get_lru_size(sid);
    }
    /* Values for remaining:
     * remaining = 0
     * - scan all elements, until a NULL is reached
     * - if empty, NULL is reached right away
     * remaining = n + 1
     * - first n elements are parsed (or until a NULL is reached)
     */
    if (remaining) remaining++;
    crawlers[sid].remaining = remaining;
    crawlers[sid].slabs_clsid = sid;
    crawlers[sid].reclaimed = 0;
    crawlers[sid].unfetched = 0;
    crawlers[sid].checked = 0;
    do_item_linktail_q((item *)&crawlers[sid]);
    crawler_count++;
    starts++;
  }

  pthread_mutex_unlock(&lru_locks[sid]);

  if (starts) {
    STATS_LOCK();
    __global_stats->lru_crawler_running = true;
    stats->lru_crawler_starts++;
    STATS_UNLOCK();
  }

  return starts;
}

int lru_crawler_start(uint8_t *ids, uint32_t remaining,
    const enum crawler_run_type type, void *data,
    void *c, const int sfd) {
#if 0
  int starts = 0;
  bool is_running;
  static rel_time_t block_ae_until = 0;
  pthread_mutex_lock(&lru_crawler_lock);
  pthread_mutex_lock(&lru_running);
  if (type == CRAWLER_AUTOEXPIRE && active_crawler_type == CRAWLER_AUTOEXPIRE) {
    pthread_mutex_unlock(&lru_crawler_lock);
    block_ae_until = *current_time + 60;
    return -1;
  }

  if (type == CRAWLER_AUTOEXPIRE && block_ae_until > *current_time) {
    pthread_mutex_unlock(&lru_crawler_lock);
    return -1;
  }

  /* Configure the module */
  if (!is_running) {
    assert(crawler_mod_regs[type] != NULL);
    active_crawler_mod.mod = pptr<crawler_module_reg_t>(crawler_mod_regs[type]);
    assert(&*(active_crawler_mod.mod) == crawler_mod_regs[type]);
    active_crawler_type = type;
    if (active_crawler_mod.mod->init != NULL) {
      active_crawler_mod.mod->init(&active_crawler_mod, data);
    }
  }

  /* we allow the autocrawler to restart sub-LRU's before completion */
  for (int sid = POWER_SMALLEST; sid < POWER_LARGEST; sid++) {
    if (ids[sid])
      starts += do_lru_crawler_start(sid, remaining);
  }
  if (starts) {
    pthread_cond_signal(&lru_crawler_cond);
  }
  pthread_mutex_unlock(&lru_crawler_lock);
  pthread_mutex_unlock(&lru_running);
  return starts;
#else
  do_lru_crawler_start(0, 0);
  return 0;
#endif
}

/* If we hold this lock, crawler can't wake up or move */
void lru_crawler_pause(void) {
  pthread_mutex_lock(&lru_crawler_lock);
}

void lru_crawler_resume(void) {
  pthread_mutex_unlock(&lru_crawler_lock);
}

int init_lru_crawler(void *arg) {
  if (lru_crawler_initialized == 0) {
    if (pthread_cond_init(&lru_crawler_cond, NULL) != 0) {
      fprintf(stderr, "Can't initialize lru crawler condition\n");
      return -1;
    }
    pthread_mutex_init(&lru_crawler_lock, NULL);
    //active_crawler_mod.c.c = NULL;
    active_crawler_mod.mod = NULL;
    active_crawler_mod.data = NULL;
    lru_crawler_initialized = 1;
  }
  return 0;
}

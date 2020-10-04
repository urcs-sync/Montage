/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  memcached - memory caching daemon
 *
 *       http://www.memcached.org/
 *
 *  Copyright 2003 Danga Interactive, Inc.  All rights reserved.
 *
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 *
 *  Authors:
 *      Anatoly Vorobey <mellon@pobox.com>
 *      Brad Fitzpatrick <brad@danga.com>
 */
#include "memcached.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <ctype.h>
#include <stdarg.h>

/* some POSIX systems need the following definition
 * to get mlockall flags out of sys/mman.h.  */
#ifndef _P1003_1B_VISIBLE
#define _P1003_1B_VISIBLE
#endif
/* need this to get IOV_MAX on some platforms. */
#ifndef __need_IOV_MAX
#define __need_IOV_MAX
#endif
#include <pwd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <limits.h>
#include <sysexits.h>
#include <stddef.h>

/* FreeBSD 4.x doesn't have IOV_MAX exposed. */
#ifndef IOV_MAX
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__GNU__)
# define IOV_MAX 1024
/* GNU/Hurd don't set MAXPATHLEN
 * http://www.gnu.org/software/hurd/hurd/porting/guidelines.html#PATH_MAX_tt_MAX_PATH_tt_MAXPATHL */
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif
#endif
#endif

/*
 * forward declarations
 */

/* THREADCACHED */
#include <utility>
int is_restart;

#ifdef PMDK 
  PMEMobjpool* pop = nullptr;
  PMEMoid root;
#endif

#ifdef JEMALLOC
  void* roots[1024];
#endif

#ifdef MAKALU
  char *base_addr = NULL;
  char *curr_addr = NULL;
#endif

/* defaults */
static void settings_init(void);

std::atomic_int *end_signal;
pthread_mutex_t begin_ops_mutex = PTHREAD_MUTEX_INITIALIZER;
/** exported globals **/
struct stats * stats;
struct settings settings;
time_t process_started;     /* when the process was started */
struct slab_rebalance slab_rebal;
volatile int slab_rebalance_signal;
static struct event_base *main_base;

struct stats_state * __global_stats;
struct thread_stats * __thread_stats;

unsigned stats_id;


enum transmit_result {
  TRANSMIT_COMPLETE,   /** All done writing. */
  TRANSMIT_INCOMPLETE, /** More data remaining to write. */
  TRANSMIT_SOFT_ERROR, /** Can't write any more right now. */
  TRANSMIT_HARD_ERROR  /** Can't write (c->state is set to conn_closing) */
};

#define REALTIME_MAXDELTA 60*60*24*30


/*
 * given time value that's either unix time or delta from current unix time, return
 * unix time. Use the fact that delta can't exceed one month (and real time value can't
 * be that low).
 */
static rel_time_t realtime(const time_t exptime) {
  /* no. of seconds in 30 days - largest possible delta exptime */

  if (exptime == 0) return 0; /* 0 means never expire */

  if (exptime > REALTIME_MAXDELTA) {
    /* if item expiration is at/before the server started, give it an
       expiration time of 1 second after the server started.
       (because 0 means don't expire).  without this, we'd
       underflow and wrap around to some large value way in the
       future, effectively making items expiring in the past
       really expiring never */
    if (exptime <= process_started)
      return (rel_time_t)1;
    return (rel_time_t)(exptime - process_started);
  } else {
    return (rel_time_t)(exptime + *current_time);
  }
}

static void settings_init(void) {
  /* By default this string should be NULL for getaddrinfo() */
  settings.maxbytes = 64 * 1024 * 1024; /* default is 64MB */
  settings.oldest_live = 0;
  settings.oldest_cas = 0;          /* supplements accuracy of oldest_live */
  settings.evict_to_free = 1;       /* push old items out of cache when memory runs out */
  settings.factor = 1.25;
  settings.chunk_size = 48;         /* space for a modest key and value */
  settings.prefix_delimiter = ':';
  settings.reqs_per_event = 20;
  settings.item_size_max = 1024 * 1024; /* The famous 1MB upper limit. */
  settings.slab_page_size = 1024 * 1024;
  settings.slab_chunk_size_max = settings.slab_page_size / 2;
  settings.lru_crawler = false;
  settings.lru_crawler_sleep = 100;
  settings.lru_crawler_tocrawl = 0;
  settings.hot_max_factor = 0.2;
  settings.warm_max_factor = 2.0;
  settings.hashpower_init = 0;
  settings.tail_repair_time = TAIL_REPAIR_TIME_DEFAULT;
  settings.crawls_persleep = 1000;
  settings.slab_automove_ratio = .8;
  settings.slab_automove_window = 30;
}

static int _store_item_copy_data(int comm, item *old_it, item *new_it, item *add_it) {
  if (comm == NREAD_APPEND) {
    memcpy(ITEM_data(new_it), ITEM_data(old_it), old_it->nbytes);
    memcpy(ITEM_data(new_it) + old_it->nbytes - 2 /* CRLF */, ITEM_data(add_it), add_it->nbytes);
  } else {
    /* NREAD_PREPEND */
    memcpy(ITEM_data(new_it), ITEM_data(add_it), add_it->nbytes);
    memcpy(ITEM_data(new_it) + add_it->nbytes - 2 /* CRLF */, ITEM_data(old_it), old_it->nbytes);
  }
  return 0;
}

/*
 * Stores an item in the cache according to the semantics of one of the set
 * commands. In threaded mode, this is protected by the cache lock.
 *
 * Returns the state of storage.
 */

std::pair<store_item_type, size_t> do_store_item(item *it, int comm, const uint32_t hv) {
  char *key = ITEM_key(it);
  item *old_it = do_item_get(key, it->nkey, hv, DONT_UPDATE);
  enum store_item_type stored = NOT_STORED;

  size_t cas = 0;
  item *new_it = NULL;
  uint32_t flags;
  if (old_it != NULL && comm == NREAD_ADD) {
    /* add only adds a nonexistent item, but promote to head of LRU */
    do_item_update(old_it);
  } else if (!old_it && (comm == NREAD_REPLACE
        || comm == NREAD_APPEND || comm == NREAD_PREPEND))
  {
    /* replace only replaces an existing value; don't store */
  } else if (comm == NREAD_CAS) {
    /* validate cas operation */
    if(old_it == NULL) {
      // LRU expired
      stored = NOT_FOUND;
    }
    else if (ITEM_get_cas(it) == ITEM_get_cas(old_it)) {
      // cas validates
      // it and old_it may belong to different classes.
      // I'm updating the stats for the one that's getting pushed out
      item_replace(old_it, it, hv);
      stored = STORED;
    } else {
      stored = EXISTS;
    }
  } else {
    int failed_alloc = 0;
    /*
     * Append - combine new and old record into single one. Here it's
     * atomic and thread-safe.
     */
    if (comm == NREAD_APPEND || comm == NREAD_PREPEND) {
      /*
       * Validate CAS
       */
      if (stored == NOT_STORED) {
        /* we have it and old_it here - alloc memory to hold both */
        /* flags was already lost - so recover them from ITEM_suffix(it) */
        FLAGS_CONV(false, old_it, flags);
        new_it = do_item_alloc(key, it->nkey, flags, old_it->exptime, it->nbytes + old_it->nbytes - 2 /* CRLF */);

        /* copy data from it and old_it to new_it */
        if (new_it == NULL || _store_item_copy_data(comm, old_it, new_it, it) == -1) {
          failed_alloc = 1;
          stored = NOT_STORED;
          // failed data copy, free up.
          if (new_it != NULL)
            item_remove(new_it);
        } else {
          it = new_it;
        }
      }
    }

    if (stored == NOT_STORED && failed_alloc == 0) {
      if (old_it != NULL) {
        item_replace(old_it, it, hv);
      } else {
        do_item_link(it, hv);
      }

      cas = ITEM_get_cas(it);

      stored = STORED;
    }
  }

  if (old_it != NULL)
    do_item_remove(old_it);         /* release our reference */
  if (new_it != NULL)
    do_item_remove(new_it);

  if (stored == STORED) {
    cas = ITEM_get_cas(it);
  }
  return std::pair<store_item_type, size_t>(stored, cas);
}

struct token_t {
  char *value;
  size_t length;
};

#define COMMAND_TOKEN 0
#define SUBCOMMAND_TOKEN 1
#define KEY_TOKEN 1

#define MAX_TOKENS 8

/*
 * adds a delta value to a numeric item.
 *
 * c     connection requesting the operation
 * it    item to adjust
 * incr  true to increment value, false to decrement
 * delta amount to adjust value by
 * buf   buffer for response string
 *
 * returns a response string to send back to the client.
 */

/*
 * We keep the current time of day in a global variable that's updated by a
 * timer event. This saves us a bunch of time() system calls (we really only
 * need to get the time once a second, whereas there can be tens of thousands
 * of requests a second) and allows us to use server-start-relative timestamps
 * rather than absolute UNIX timestamps, a space savings on systems where
 * sizeof(time_t) > sizeof(unsigned int).
 */
volatile rel_time_t *current_time;
static struct event clockevent;

/* libevent uses a monotonic clock when available for event scheduling. Aside
 * from jitter, simply ticking our internal timer here is accurate enough.
 * Note that users who are setting explicit dates for expiration times *must*
 * ensure their clocks are correct before starting memcached. */
static void clock_handler(const int fd, const short which, void *arg) {
  struct timeval t = {.tv_sec = 1, .tv_usec = 0};
  static bool initialized = false;
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
  static bool monotonic = false;
  static time_t monotonic_start;
#endif

  if (initialized) {
    /* only delete the event if it's actually there. */
    evtimer_del(&clockevent);
  } else {
    initialized = true;
    /* process_started is initialized to time() - 2. We initialize to 1 so
     * flush_all won't underflow during tests. */
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
      monotonic = true;
      monotonic_start = ts.tv_sec - ITEM_UPDATE_INTERVAL - 2;
    }
#endif
  }

  // While we're here, check for hash table expansion.
  // This function should be quick to avoid delaying the timer.
  uint32_t curr_items = 0;
  for(uint32_t i = 0; i < NUM_STATS; ++i)
    curr_items += __thread_stats[i].curr_items.load();
  assoc_start_expand(curr_items);

  evtimer_set(&clockevent, clock_handler, 0);
  event_base_set(main_base, &clockevent);
  evtimer_add(&clockevent, &t);

#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
  if (monotonic) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
      return;
    *current_time = (rel_time_t) (ts.tv_sec - monotonic_start);
    return;
  }
#endif
  {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *current_time = (rel_time_t) (tv.tv_sec - process_started);
  }
}

static void sig_handler(const int sig) {
  printf("Signal handled: %s.\n", strsignal(sig));
  exit(EXIT_SUCCESS);
}

#ifndef HAVE_SIGIGNORE
static int sigignore(int sig) {
  struct sigaction sa = { .sa_handler = SIG_IGN, .sa_flags = 0 };

  if (sigemptyset(&sa.sa_mask) == -1 || sigaction(sig, &sa, 0) == -1) {
    return -1;
  }
  return 0;
}
#endif
static std::atomic_int *pause_sig;
static std::atomic_int *num_lookers;

static void init_slab_stats(slab_stats * ptr) {
  ptr->set_cmds = 0;
  ptr->get_hits = 0;
  ptr->touch_hits = 0;
  ptr->delete_hits = 0;
  ptr->cas_hits = 0;
  ptr->cas_badval = 0;
  ptr->incr_hits = 0;
  ptr->decr_hits = 0;
}

static void init_thread_stats(thread_stats * ptr) {
  ptr->get_cmds = 0; 
  ptr->get_misses = 0; 
  ptr->get_expired = 0; 
  ptr->get_flushed = 0; 
  ptr->touch_cmds = 0; 
  ptr->touch_misses = 0; 
  ptr->delete_misses = 0; 
  ptr->incr_misses = 0; 
  ptr->decr_misses = 0; 
  ptr->cas_misses = 0; 
  ptr->meta_cmds = 0; 
  ptr->bytes_read = 0; 
  ptr->bytes_written = 0; 
  ptr->flush_cmds = 0; 
  ptr->conn_yields = 0; /* # of yields for connections (-R option)*/ 
  ptr->auth_cmds = 0; 
  ptr->auth_errors = 0; 
  ptr->idle_kicks = 0; /* idle connections killed */ 
  ptr->response_obj_oom = 0; 
  ptr->read_buf_oom = 0;
  ptr->curr_items = 0;
  ptr->curr_bytes = 0;
  for(unsigned i = 0; i < MAX_NUMBER_OF_SLAB_CLASSES; ++i)
    init_slab_stats(&ptr->slab_stats[i]);
  for(unsigned i = 0; i < POWER_LARGEST; ++i)
    ptr->lru_hits[i] = 0;
  ptr->total_items = 0;
}

static void init_global_stats(stats_state * ptr){
  ptr->curr_conns = 0;
  ptr->hash_bytes = 0;       /* size used for hash tables */
  ptr->conn_structs = 0;
  ptr->reserved_fds = 0;
  ptr->hash_power_level = 0; /* Better hope it's not over 9000; */
  ptr->hash_is_expanding = 0; /* If the hash table is being expanded */
  // std::atomic<bool>ptr->accepting_conns = 0;   whether we are currently accepting 
  ptr->slab_reassign_running = 0; /* slab reassign in progress */
  ptr->lru_crawler_running = 0; /* crawl in progress */
}

static void stats_init(void) {
  if (is_restart){
    __thread_stats = pm_get_root<thread_stats>(RPMRoot::TStats);
    __global_stats = pm_get_root<struct stats_state>(RPMRoot::GStats);
    stats = pm_get_root<struct stats>(RPMRoot::Stats);
  } else {
    __thread_stats = (thread_stats*)pm_malloc(sizeof(thread_stats)*NUM_STATS);
    __global_stats = (stats_state*)pm_malloc(sizeof(stats_state));
    stats = (struct stats*)pm_malloc(sizeof(struct stats));
    pm_set_root(__thread_stats, RPMRoot::TStats);
    pm_set_root(__global_stats, RPMRoot::GStats);
    pm_set_root(stats, RPMRoot::Stats);
    for(unsigned i = 0; i < NUM_STATS; ++i)
      init_thread_stats(__thread_stats + i);
    init_global_stats(__global_stats);
    memset(stats, 0, sizeof(struct stats));
  }
  stats_id = rand() & STATS_HASH;
}
// run this regardless of whether you're a server or a client
void agnostic_init(){
  srand(time(0));
  if (!is_restart){
    end_signal = (std::atomic_int*)pm_malloc(sizeof(std::atomic_int));
    current_time = (rel_time_t*)pm_malloc(sizeof(rel_time_t));
    pause_sig = (std::atomic_int*)pm_malloc(sizeof(std::atomic_int));
    num_lookers = (std::atomic_int*)pm_malloc(sizeof(std::atomic_int));
    assert(end_signal != nullptr &&
        current_time != nullptr &&
        pause_sig != nullptr &&
        num_lookers != nullptr);
    new (end_signal) std::atomic_int();
    new (pause_sig) std::atomic_int();
    new (num_lookers) std::atomic_int();
    *pause_sig = 0;
    *num_lookers = 0;
    pm_set_root(num_lookers, RPMRoot::NLookers);
    pm_set_root(pause_sig, RPMRoot::PSig);
    pm_set_root(end_signal, RPMRoot::EndSignal);
    pm_set_root((void*)current_time, RPMRoot::CTime);
  } else {
    end_signal = pm_get_root<std::atomic_int >(RPMRoot::EndSignal);
    current_time = pm_get_root<rel_time_t>(RPMRoot::CTime);
    num_lookers = pm_get_root<std::atomic_int>(RPMRoot::NLookers);
    pause_sig = pm_get_root<std::atomic_int>(RPMRoot::PSig);
  }
  enum {
    MAXCONNS_FAST = 0,
    HASHPOWER_INIT,
    NO_HASHEXPAND,
    TAIL_REPAIR_TIME,
    HASH_ALGORITHM,
    LRU_CRAWLER,
    LRU_CRAWLER_SLEEP,
    LRU_CRAWLER_TOCRAWL,
    LRU_MAINTAINER,
    HOT_LRU_PCT,
    WARM_LRU_PCT,
    HOT_MAX_FACTOR,
    WARM_MAX_FACTOR,
    TEMPORARY_TTL,
    IDLE_TIMEOUT,
    WATCHER_LOGBUF_SIZE,
    WORKER_LOGBUF_SIZE,
    TRACK_SIZES,
    NO_INLINE_ASCII_RESP,
    MODERN,
    NO_MODERN,
    NO_CHUNKED_ITEMS,
    NO_MAXCONNS_FAST,
    INLINE_ASCII_RESP,
    NO_LRU_CRAWLER,
    NO_LRU_MAINTAINER
  };
  /* init settings */
  settings_init();
  memcached_thread_init();
  items_init();
  // initialize stats
  stats_init();

  /* Run regardless of initializing it later */
  //init_lru_maintainer();

  /* set stderr non-buffering (for running under, say, daemontools) */
  setbuf(stderr, NULL);

  /* initialize other stuff */
  assoc_init(settings.hashpower_init);
}

static inline unsigned int rdpkru(void) {
  unsigned int eax, edx;
  unsigned int ecx = 0;
  unsigned int pkru;

  asm volatile(".byte 0x0f,0x01,0xee\n\t" : "=a"(eax), "=d"(edx) : "c"(ecx));
  pkru = eax;
  return pkru;
}

void* server_thread (void *pargs) {
  // unsigned int mynt= rdpkru();
  // printf("enters server_thread() %x\n", mynt);
  // fflush(stdout);
  *end_signal = 0;
  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  main_base = event_base_new_with_config(ev_config);
  event_config_free(ev_config);


  // if (is_restart) {
  //   pm_recover();
  // }
  /* handle SIGINT and SIGTERM */
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  if (sigignore(SIGPIPE) == -1) {
    perror("failed to ignore SIGPIPE; sigaction");
    exit(EX_OSERR);
  }
  /* start up worker threads if MT mode */
  // num_threads
  init_lru_crawler(NULL);

  //assert(start_assoc_maintenance_thread() != -1);
  //assert(start_item_crawler_thread() == 0);
  //assert(start_lru_maintainer_thread(NULL) == 0);
  /* initialise clock event */
  clock_handler(0, 0, 0);

  /* enter the event loop */
  // TODO chnage to wait on maintenance threads
  pthread_mutex_unlock(&begin_ops_mutex);
  while(end_signal->load() == 0){
    sleep(1);
    event_base_loop(main_base, EVLOOP_ONCE);
  }

  stop_assoc_maintenance_thread();
  pm_close();
  return NULL;
}

static void inc_lookers (void){
  if (num_lookers->fetch_add(1) < 0){
    --(*num_lookers);
    while(*pause_sig);
    ++(*num_lookers);
  }
}

static void dec_lookers (void){
  --(*num_lookers);
}

void pause_accesses(void){
  *pause_sig = 1;
  num_lookers->fetch_sub(1000);
  while(*num_lookers != -1000);
}

void unpause_accesses(void){
  num_lookers->fetch_add(1000);
  *pause_sig = 0;
}

char buf[32];

enum delta_result_type do_add_delta(const char *key,
    const size_t nkey, const bool incr,
    const uint64_t delta, uint64_t *value_ptr, const uint32_t hv) {
  char *ptr;
  uint64_t value;
  int res;
  item *it;

  it = do_item_get(key, nkey, hv, DONT_UPDATE);
  if (!it) {
    return DELTA_ITEM_NOT_FOUND;
  }

  if (it->nbytes <= 2) {
    do_item_remove(it);
    return NON_NUMERIC;
  }

  ptr = ITEM_data(it);

  if (!safe_strtoull(ptr, &value)) {
    do_item_remove(it);
    return NON_NUMERIC;
  }

  if (incr) {
    value += delta;
  } else {
    if(delta > value) {
      value = 0;
    } else {
      value -= delta;
    }
  }
  *value_ptr = value;

  itoa_u64(value, buf);
  res = strlen(buf);
  /* refcount == 2 means we are the only ones holding the item, and it is
   * linked. We hold the item's lock in this function, so refcount cannot
   * increase. */
  if (res + 2 <= it->nbytes && it->refcount == 2) { 
    /* replace in-place */
    memcpy(ITEM_data(it), buf, res);
    memset(ITEM_data(it) + res, ' ', it->nbytes - res - 2);
    do_item_update(it);
  } else if (it->refcount > 1) {
    item *new_it;
    uint32_t flags;
    FLAGS_CONV(nullptr, it, flags);
    new_it = do_item_alloc(ITEM_key(it), it->nkey, flags, it->exptime, res + 2);
    if (new_it == 0) {
      do_item_remove(it);
      return EOM;
    }
    memcpy(ITEM_data(new_it), buf, res);
    memcpy(ITEM_data(new_it) + res, "\r\n", 2);
    item_replace(it, new_it, hv);
    do_item_remove(new_it);       /* release our reference */
  } else {
    /* Should never get here. This means we somehow fetched an unlinked
     * item. TODO: Add a counter? */
    if (it->refcount == 1)
      do_item_remove(it);
    if (incr) {
      __thread_stats[stats_id].incr_misses.fetch_add(1);
    } else {
      __thread_stats[stats_id].decr_misses.fetch_add(1);
    }
    return DELTA_ITEM_NOT_FOUND;
  }
    if (incr) {
      __thread_stats[stats_id].slab_stats[it->slabs_clsid].incr_hits.fetch_add(1);
    } else {
      __thread_stats[stats_id].slab_stats[it->slabs_clsid].decr_hits.fetch_add(1);
    }
  do_item_remove(it);         /* release our reference */
  return OK;
}

// Race conditions don't happen in get operations. We never do in place writes,
// only full replacements. Whenever you do a item_get operation, it increments
// the refcounts and prevents it from being freed. 
memcached_return_t
pku_memcached_get(const char* key, size_t nkey, char* &buffer, size_t* buffLen,
    uint32_t *flags){
  // Can't use user pointers inside protected function. Copy to private buffer
  // before resources are acquired
  char * key_prot = (char*)pm_malloc(nkey);
  memcpy(key_prot, key, nkey);

  // increment number of readers/writers
  inc_lookers();

  // Wentao: get doesn't need BEGIN_OP---it's OK to read things as soon as they are visible
  // Get the item using the protected key
  item* it = item_get(key_prot, nkey, 1);
  // We don't need the key anymore
  pm_free(key_prot);
  if (it == NULL){
    // stats
    __thread_stats[stats_id].get_cmds.fetch_add(1);
    __thread_stats[stats_id].get_misses.fetch_add(1);
    return MEMCACHED_NOTFOUND;
  }
  __thread_stats[stats_id].get_cmds.fetch_add(1);
  __thread_stats[stats_id].lru_hits[it->slabs_clsid].fetch_add(1);

  // We found the item so we need somewhere to copy its data to while
  // important resources are held (in this case, reference counts)
  char * dat_prot = (char*)pm_malloc(it->nbytes);
  memcpy(dat_prot, ITEM_data(it), it->nbytes);
  size_t flag_prot = it->it_flags;
  size_t buffLen_prot = it->nbytes;
  
  // release our reference
  item_remove(it);
  dec_lookers();

  // create a buffer for the user if they didn't provide one
  if (buffer == NULL)
    buffer = (char*)malloc(buffLen_prot);

  // copy protected data to user-accessible locationss
  *buffLen = buffLen_prot;
  *flags = flag_prot;
  memcpy(buffer, dat_prot, *buffLen);

  // free data buffer
  pm_free(dat_prot);
  return MEMCACHED_SUCCESS;
}

// Is this good? If the user doesn't fetch all the entries, some entries might
// be unfreeable. TODO - change this
memcached_return_t
pku_memcached_mget(const char * const *keys, const size_t *key_length,
    size_t number_of_keys, item **list){
  for(unsigned i = 0; i < number_of_keys; ++i)
    list[i] = item_get(keys[i], key_length[i], 1);
  return MEMCACHED_SUCCESS;
} 

memcached_return_t
pku_memcached_insert(const char* key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime){
  // Reduce the number of allocations we have to do
  char * key_prot = (char*)pm_malloc(nkey + datan);
  if (key_prot == NULL) 
    return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
  char * dat_prot = key_prot + nkey;

  memcpy(key_prot, key, nkey);
  memcpy(dat_prot, data, datan);
  inc_lookers();
  item *it = item_alloc(key_prot, nkey, 0, realtime(exptime), datan + 2);

  if (it != NULL) {
    // increase # of set cmds in stats
    __thread_stats[stats_id].slab_stats[ITEM_clsid(it)].set_cmds.fetch_add(1);
    memcpy(ITEM_data(it), dat_prot, datan);
    memcpy(ITEM_data(it) + datan, "\r\n", 2);
    uint32_t hv = tcd_hash(key, nkey);
    auto pr = do_store_item(it, NREAD_ADD, hv);
    switch(pr.first){
      case NOT_FOUND:
        pm_free(key_prot);
        return MEMCACHED_NOTFOUND;
      case TOO_LARGE: 
        pm_free(key_prot);
        return MEMCACHED_E2BIG;
      case NO_MEMORY:
        pm_free(key_prot);
        return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
      case NOT_STORED:
        pm_free(key_prot);
        return MEMCACHED_NOTSTORED;
      default:
        break;
    }
    item_remove(it);         /* release our reference */
  } else {
    perror("SERVER_ERROR Out of memory allocating new item");
    pm_free(key_prot);
    return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
  }
  dec_lookers();
  pm_free(key_prot);
  return MEMCACHED_SUCCESS;
}

memcached_return_t
pku_memcached_set(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime){
  // increase # of set cmds in stats
  char * key_prot = (char*)pm_malloc(nkey + datan);
  if (key_prot == NULL)
    return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
  char * dat_prot = key_prot + nkey;
  memcpy(key_prot, key, nkey);
  memcpy(dat_prot, data, datan);
  inc_lookers();
  item *it = item_alloc(key, nkey, 0, realtime(exptime), datan + 2);

  if (it == 0) {
    if (! item_size_ok(nkey, 0, datan)) {
      pm_free(key_prot);
      fprintf(stderr, "SERVER_ERROR too big for the cache\n");
      return MEMCACHED_KEY_TOO_BIG; // Maybe make this more informative
    } else {
      pm_free(key_prot);
      fprintf(stderr, "SERVER_ERROR out of memory storing object");
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    }
    it = item_get(key_prot, nkey, DONT_UPDATE);
    if (it) {
      item_unlink(it); // remove item from table
      item_remove(it); // lazily delete it
    }
    // If we're storing, don't allow data to overwrite to persist in cache,
    // we prefer old data to not exist than pollute the cache
    pm_free(key_prot);
    return MEMCACHED_NOTSTORED;
  }
  
  // increase # of set cmds in stats
  __thread_stats[stats_id].slab_stats[ITEM_clsid(it)].set_cmds.fetch_add(1);
  memcpy(ITEM_data(it), dat_prot, datan);
  memcpy(ITEM_data(it) + datan, "\r\n", 2);
  // Wentao: persist, pretire, and preclaim are handled inside store_item()
  auto res = store_item(it, NREAD_SET);
  //release our reference
  item_remove(it);         

  dec_lookers();
  switch(res) {
    case EXISTS:
    case STORED:
      break;
    case NOT_FOUND:
      pm_free(key_prot);
      return MEMCACHED_NOTFOUND;
    case TOO_LARGE: 
      pm_free(key_prot);
      return MEMCACHED_E2BIG;
    case NO_MEMORY:
      pm_free(key_prot);
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    case NOT_STORED:
      pm_free(key_prot);
      return MEMCACHED_NOTSTORED;
  }
  pm_free(key_prot);
  return MEMCACHED_STORED;
}

memcached_return_t
pku_memcached_flush(uint32_t exptime){
  pause_accesses();
  __thread_stats[stats_id].flush_cmds.fetch_add(1);
  rel_time_t new_oldest = 0;
  if (exptime > 0) {
    new_oldest = realtime(exptime);
  } else {
    new_oldest = *current_time;
  }
  settings.oldest_live = new_oldest;
  unpause_accesses();
  return MEMCACHED_SUCCESS; 
}

memcached_return_t
pku_memcached_delete(const char * key, size_t nkey, uint32_t exptime){
  char * key_prot = (char*)pm_malloc(nkey);
  memcpy(key_prot, key, nkey);
  inc_lookers();
  item *it;
  memcached_return_t ret;
  uint32_t hv;
  it = item_get_locked(key_prot, nkey, DONT_UPDATE, &hv);
  if (it) {
    do_item_unlink(it, hv);
    do_item_remove(it);      /* release our reference */ // has our break
    __thread_stats[stats_id].slab_stats[it->slabs_clsid].delete_hits.fetch_add(1);
    ret = MEMCACHED_SUCCESS;
  } else {
    __thread_stats[stats_id].delete_misses.fetch_add(1);
    ret = MEMCACHED_FAILURE;
  }
  item_unlock(hv);
  dec_lookers();
  pm_free(key_prot);
  return ret;
}

memcached_return_t
pku_memcached_append(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags) {
  char * key_prot = (char*)pm_malloc(nkey + datan);
  char * dat_prot = key_prot + nkey; 
  memcpy(key_prot, key, nkey);
  memcpy(dat_prot, data, datan);
  inc_lookers();
  item *it = item_alloc(key_prot, nkey, 0, 0, datan+2);

  if (it == 0) {
    if (! item_size_ok(nkey, 0, datan + 2)) {
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_E2BIG;
    } else {
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    }
  }
  __thread_stats[stats_id].slab_stats[ITEM_clsid(it)].set_cmds.fetch_add(1);
  memcpy(ITEM_data(it), dat_prot , datan);
  memcpy(ITEM_data(it) + datan, "\r\n", 2);
  if (store_item(it, NREAD_APPEND) != STORED){
    dec_lookers();
    pm_free(key_prot);
    return MEMCACHED_NOTSTORED;
  }
  item_remove(it);
  dec_lookers();
  pm_free(key_prot);
  return MEMCACHED_STORED;  
}

memcached_return_t
pku_memcached_prepend(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags) {
  char * key_prot = (char*)pm_malloc(nkey + datan);
  char * dat_prot = key_prot + nkey; 
  memcpy(key_prot, key, nkey);
  memcpy(dat_prot, data, datan);
  inc_lookers();
  item *it = item_alloc(key_prot, nkey, 0, 0, datan+2);

  if (it == 0) {
    if (! item_size_ok(nkey, 0, datan + 2)) {
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_E2BIG;
    } else {
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    }
  }
  __thread_stats[stats_id].slab_stats[ITEM_clsid(it)].set_cmds.fetch_add(1);
  memcpy(ITEM_data(it), dat_prot , datan);
  memcpy(ITEM_data(it) + datan, "\r\n", 2);
  if (store_item(it, NREAD_PREPEND) != STORED){
    dec_lookers();
    pm_free(key_prot);
    return MEMCACHED_NOTSTORED;
  }
  item_remove(it);
  dec_lookers();
  pm_free(key_prot);
  return MEMCACHED_STORED;  
}

memcached_return_t
pku_memcached_replace(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags){
  char * key_prot = (char*)pm_malloc(nkey + datan);
  char * dat_prot = key_prot + nkey;
  memcpy(key_prot, key, nkey);
  memcpy(dat_prot, data, datan);
  inc_lookers();
  item *it = item_alloc(key_prot, nkey, 0, 0, datan+2);

  if (it == 0) {
    if (! item_size_ok(nkey, 0, datan + 2)) {
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_E2BIG;
    } else {
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    }
  }
  
  // increase # of set cmds in stats
  __thread_stats[stats_id].slab_stats[ITEM_clsid(it)].set_cmds.fetch_add(1);

  memcpy(ITEM_data(it), dat_prot, datan);
  memcpy(ITEM_data(it) + datan, "\r\n", 2);
  switch(store_item(it, NREAD_REPLACE)) {
    case EXISTS:
    case STORED:
      break;
    case NOT_FOUND:
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_NOTFOUND;
    case TOO_LARGE: 
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_E2BIG;
    case NO_MEMORY:
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    case NOT_STORED:
      dec_lookers();
      pm_free(key_prot);
      return MEMCACHED_NOTSTORED;
  }
  item_remove(it);
  dec_lookers();
  pm_free(key_prot);
  return MEMCACHED_SUCCESS;  

}


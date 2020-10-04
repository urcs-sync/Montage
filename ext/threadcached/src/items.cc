/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "memcached.h"
#include "bipbuffer.h"
#include "slab_automove.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <poll.h>

/* Forward Declarations */
static void item_link_q(item *it);
static void item_unlink_q(item *it);

#define LARGEST_ID POWER_LARGEST

// Standard memcached has multiple LRUs per slabclass
// For simplicity, I am choosing the simplest LRU implementation possible...
// slabclass ID = LRU ID
// This can be improved later, but for right now we're looking for apples to
// apples for the paper. In theory we could do 
// LRU ID = (slbcls id << 2) | (hash & 3);
// This would get us a larger amount of slabs deterministically but without the
// nice segmentation of the standard Memcached LRUs
 static pptr<item> *heads;
 static pptr<item> *tails;
 static unsigned int *sizes; 
static uint64_t *sizes_bytes; 

static volatile int do_run_lru_maintainer_thread = 0;
static int lru_maintainer_initialized = 0;
static pthread_mutex_t * lru_maintainer_lock = NULL; //PTHREAD_MUTEX_INITIALIZER;
//
// How many slab classes are necessary to contain at a maximum our 3MB items
// given a "slab" growth factor of 1.25? 
// log_10(3 * 1024 * 1024 / 64)/log_10(1.25) = 48.411
#define SLAB_CLASSES 50
#define CHUNK_ALIGN_BYTES 8
void items_init(){
  if (is_restart){
    // get roots
    heads = (pptr<item>*)pm_get_root<item*>(RPMRoot::Heads);
    tails = (pptr<item>*)pm_get_root<item*>(RPMRoot::Tails);
    sizes = pm_get_root<unsigned int>(RPMRoot::Sizes);
    sizes_bytes = pm_get_root<uint64_t>(RPMRoot::SizesBytes);
    lru_maintainer_lock = pm_get_root<pthread_mutex_t>(RPMRoot::LRUMaintainerLock);
  } else {
    heads = (pptr<item>*)pm_calloc(sizeof(pptr<item>), SLAB_CLASSES);
    tails = (pptr<item>*)pm_calloc(sizeof(pptr<item>), SLAB_CLASSES);
    sizes = (unsigned int*)pm_calloc(sizeof(unsigned int), SLAB_CLASSES);
    sizes_bytes = (uint64_t*)pm_calloc(sizeof(uint64_t), SLAB_CLASSES);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, 1);
    lru_maintainer_lock = (pthread_mutex_t*)pm_malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(lru_maintainer_lock, &attr);
    new (heads) pptr<item> [SLAB_CLASSES];
    new (tails) pptr<item> [SLAB_CLASSES];
    for(unsigned i = 0; i < SLAB_CLASSES; ++i){
      heads[i] = nullptr;
      tails[i] = nullptr;
    }

    // This magic number is the defualt one given in base memcached. It is
    // usually customizable.
    unsigned int size = sizeof(item) + 48;
    /* Make sure items are always n-byte aligned */
    for (unsigned int i = 0; i < SLAB_CLASSES; ++i){
      if (size % CHUNK_ALIGN_BYTES)
        size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
      sizes[i] = size;
      sizes_bytes[i] = 0;
      // 1.25 is the default growth factor
      size *= 1.25;
    }

    pm_set_root(heads, RPMRoot::Heads);
    pm_set_root(tails, RPMRoot::Tails);
    pm_set_root(sizes, RPMRoot::Sizes);
    pm_set_root(sizes_bytes, RPMRoot::SizesBytes);
  }
}

void item_stats_reset(void) {
}

/* called with class lru lock held */
void do_item_stats_add_crawl(const int i, const uint64_t reclaimed,
    const uint64_t unfetched, const uint64_t checked) {
}

typedef struct _lru_bump_buf {
  struct _lru_bump_buf *prev;
  struct _lru_bump_buf *next;
  pthread_mutex_t mutex;
  bipbuf_t *buf;
  uint64_t dropped;
} lru_bump_buf;

typedef struct {
  item *it;
  uint32_t hv;
} lru_bump_entry;

static lru_bump_buf *bump_buf_head = NULL;
static lru_bump_buf *bump_buf_tail = NULL;
static pthread_mutex_t bump_buf_lock = PTHREAD_MUTEX_INITIALIZER;
/* TODO: tunable? Need bench results */
#define LRU_BUMP_BUF_SIZE 8192

int item_is_flushed(item *it) {
  rel_time_t oldest_live = settings.oldest_live;
  uint64_t cas = ITEM_get_cas(it);
  uint64_t oldest_cas = settings.oldest_cas;
  if (oldest_live == 0 || oldest_live > *current_time)
    return 0;
  if ((it->time <= oldest_live)
      || (oldest_cas != 0 && cas != 0 && cas < oldest_cas)) {
    return 1;
  }
  return 0;
}

/* must be locked before call */
unsigned int do_get_lru_size(uint32_t id) {
  return sizes[id];
}

/* Enable this for reference-count debugging. */
#if 0
# define DEBUG_REFCNT(it,op) \
  fprintf(stderr, "item %x refcnt(%c) %d %c%c%c\n", \
      it, op, it->refcount, \
      (it->it_flags & ITEM_LINKED) ? 'L' : ' ', \
      (it->it_flags & ITEM_SLABBED) ? 'S' : ' ')
#else
# define DEBUG_REFCNT(it,op) while(0)
#endif

inline int round_up(int numToRound, int multiple)
{
  if (multiple == 0)
    return numToRound;

  int remainder = numToRound % multiple;
  if (remainder == 0)
    return numToRound;

  return numToRound + multiple - remainder;
}

/**
 * Generates the variable-sized part of the header for an object.
 *
 * nkey    - The length of the key
 * flags   - key flags
 * nbytes  - Number of bytes to hold value and addition CRLF terminator
 * suffix  - Buffer for the "VALUE" line suffix (flags, size).
 * nsuffix - The length of the suffix is stored here.
 *
 * Returns the total size of the header.
 */
static size_t item_make_header(const uint8_t nkey, const unsigned int flags, const int nbytes,
    char *suffix, uint8_t *nsuffix) {
  if (flags == 0) {
    *nsuffix = 0;
  } else {
    *nsuffix = sizeof(flags);
  }
  // round up to multiple of cache line size (64B)
  return round_up(sizeof(item) + nkey + *nsuffix + nbytes, 64);
}

item *do_item_alloc_pull(const size_t ntotal, const unsigned int id) {
  item *it = NULL;
  int i;
  /* If no memory is available, attempt a direct LRU juggle/eviction */
  /* This is a race in order to simplify lru_pull_tail; in cases where
   * locked items are on the tail, you want them to fall out and cause
   * occasional OOM's, rather than internally work around them.
   * This also gives one fewer code path for slab alloc/free
   */
  for (i = 0; i < 10; i++) {
    uint64_t total_bytes = 0;
    /* Try to reclaim memory first */
    lru_pull_tail(id, COLD_LRU, 0, 0, 0, NULL);
    it = (item*)pm_malloc(ntotal);

    if (it == NULL) {
      // We send '0' in for "total_bytes" as this routine is always
      // pulling to evict, or forcing HOT -> COLD migration.
      // As of this writing, total_bytes isn't at all used with COLD_LRU.
      if (lru_pull_tail(id, COLD_LRU, total_bytes, LRU_PULL_EVICT, 0, NULL) <= 0) {
        break;
      }
    } else {
      new (it) item();

      break;
    }
  }

  return it;
}
#define ITEM_MAX_SZ (2 * 1024 * 1024)

static int get_slab_clsid(unsigned sz){
  for(unsigned i = 0; i < SLAB_CLASSES; ++i){
    if (sizes[i] >= sz) return i;
  }
  return -1;
}

item *do_item_alloc(const char *key, const size_t nkey, const unsigned int flags,
    const rel_time_t exptime, const int nbytes) {
  uint8_t nsuffix;
  item *it = NULL;
  char suffix[40];
  // Avoid potential underflows.
  if (nbytes < 2)
    return 0;

  size_t ntotal = item_make_header(nkey + 2, flags, nbytes, suffix, &nsuffix);

  int id = get_slab_clsid(ntotal); // & 63
  if (ntotal > ITEM_MAX_SZ) return 0;
  if (id == -1) return 0;

  it = do_item_alloc_pull(ntotal, id);

  assert(it->it_flags == 0);
  //assert(it != heads[id]);
  /* Items are initially loaded into the HOT_LRU. This is '0' but I want at
   * least a note here. Compiler (hopefully?) optimizes this out.
   */
  // if (settings.temp_lru &&
  //    exptime - *current_time <= (int)settings.temporary_ttl) {
  //  id |= TEMP_LRU;
  // } else {
    /* There is only COLD in compat-mode */
  //  id |= COLD_LRU;
  //}
  it->slabs_clsid = id;
  sizes_bytes[id] += ntotal;

  it->next = it->prev = it->h_next = 0;
  it->refcount = 1;


  DEBUG_REFCNT(it, '*');
  it->nkey = nkey;
  it->nbytes = nbytes;
  memcpy(ITEM_key(it), key, nkey);
  it->exptime = exptime;
  if (nsuffix > 0) {
    memcpy(ITEM_suffix(it), &flags, sizeof(flags));
  }

  it->h_next = 0;

  return it;
}

void item_free(item *it) {
  assert((it->it_flags & ITEM_LINKED) == 0);
  assert(it != heads[it->slabs_clsid]);
  assert(it != tails[it->slabs_clsid]);
  assert(it->refcount == 0);

  /* so slab size changer can tell later if item is already free or not */
//  sizes_bytes[it->slabs_clsid] -= sizeof(item) + 
#ifdef MONTAGE
  PRECLAIM(it);
#else
  pm_free(it);
#endif
}

/**
 * Returns true if an item will fit in the cache (its size does not exceed
 * the maximum for a cache entry.)
 */
bool item_size_ok(const size_t nkey, const int flags, const int nbytes) {
  char prefix[40];
  uint8_t nsuffix;
  if (nbytes < 2)
    return false;

  size_t ntotal = item_make_header(nkey + 2, flags, nbytes,
      prefix, &nsuffix);

  return ntotal <= ITEM_MAX_SZ;
}

/* fixing stats/references during warm start */
void do_item_link_fixup(item *it) {
  pptr<item> *head, *tail;
  int ntotal = ITEM_ntotal(it);
  uint32_t hv = tcd_hash(ITEM_key(it), it->nkey);
  assoc_insert(it, hv);

  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];
  if (it->prev == 0 && *head == 0) *head = it;
  if (it->next == 0 && *tail == 0) *tail = it;
  sizes[it->slabs_clsid]++;
  sizes_bytes[it->slabs_clsid] += ntotal;

  __thread_stats[stats_id].curr_bytes.fetch_add(ntotal);
  __thread_stats[stats_id].curr_items.fetch_add(1);
  __thread_stats[stats_id].total_items.fetch_add(1);

  return;
}

static void do_item_link_q(item *it) { /* item is the new head */
  pptr<item> *head, *tail;
  assert((it->it_flags & ITEM_SLABBED) == 0);

  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];
  assert(it != *head);
  assert((*head != NULL && *tail != NULL) || (*head == 0 && *tail == 0));
  it->prev = 0;
  it->next = *head;
  if (it->next != NULL) it->next->prev = it;
  *head = it;
  if (*tail == 0) *tail = it;
  sizes[it->slabs_clsid]++;
  sizes_bytes[it->slabs_clsid] += ITEM_ntotal(it);

  return;
}

static void item_link_q(item *it) {
  pthread_mutex_lock(&lru_locks[it->slabs_clsid]);
  do_item_link_q(it);
  pthread_mutex_unlock(&lru_locks[it->slabs_clsid]);
}

static void do_item_unlink_q(item *it) {
  pptr<item> *head, *tail;
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  if (*head == it) {
    assert(it->prev == 0);
    *head = it->next;
  }
  if (*tail == it) {
    assert(it->next == 0);
    *tail = it->prev;
  }
  assert(it->next != it);
  assert(it->prev != it);

  if (it->next != NULL) it->next->prev = it->prev;
  if (it->prev != NULL) it->prev->next = it->next;
  sizes[it->slabs_clsid]--;
  sizes_bytes[it->slabs_clsid] -= ITEM_ntotal(it);

  return;
}

static void item_unlink_q(item *it) {
  pthread_mutex_lock(&lru_locks[it->slabs_clsid]);
  do_item_unlink_q(it);
  pthread_mutex_unlock(&lru_locks[it->slabs_clsid]);
}

int do_item_link(item *it, const uint32_t hv) {
  assert((it->it_flags & (ITEM_LINKED|ITEM_SLABBED)) == 0);
  it->it_flags |= ITEM_LINKED;
  it->time = *current_time;

  __thread_stats[stats_id].curr_bytes.fetch_add(ITEM_ntotal(it));
  __thread_stats[stats_id].curr_items.fetch_add(1);
  __thread_stats[stats_id].total_items.fetch_add(1);

  /* Allocate a new CAS ID on link. */
  assoc_insert(it, hv);
  item_link_q(it);
  refcount_incr(it);
  return 1;
}

void do_item_unlink(item *it, const uint32_t hv) {
  if ((it->it_flags & ITEM_LINKED) != 0) {
    it->it_flags &= ~ITEM_LINKED;
    __thread_stats[stats_id].curr_bytes.fetch_sub(ITEM_ntotal(it));
    __thread_stats[stats_id].curr_items.fetch_sub(1);
    assoc_delete(ITEM_key(it), it->nkey, hv);
    item_unlink_q(it);
    do_item_remove(it);
  }
}

/* FIXME: Is it necessary to keep this copy/pasted code? */
void do_item_unlink_nolock(item *it, const uint32_t hv) {
  if ((it->it_flags & ITEM_LINKED) != 0) {
    it->it_flags &= ~ITEM_LINKED;
    __thread_stats[stats_id].curr_bytes.fetch_sub(ITEM_ntotal(it));
    __thread_stats[stats_id].curr_items.fetch_sub(1);
    assoc_delete(ITEM_key(it), it->nkey, hv);
    do_item_unlink_q(it);
    do_item_remove(it);
  }
}

void do_item_remove(item *it) {
  assert((it->it_flags & ITEM_SLABBED) == 0);
  assert(it->refcount > 0);

  if (refcount_decr(it) == 0) {
    item_free(it);
  }
}

/* Bump the last accessed time, or relink if we're in compat mode */
void do_item_update(item *it) {
  /* Hits to COLD_LRU immediately move to WARM. */
  if (it->time < *current_time - ITEM_UPDATE_INTERVAL) {
    assert((it->it_flags & ITEM_SLABBED) == 0);

    if ((it->it_flags & ITEM_LINKED) != 0) {
      it->time = *current_time;
      item_unlink_q(it);
      item_link_q(it);
    }
  }
}

int do_item_replace(item *it, item *new_it, const uint32_t hv) {
  assert((it->it_flags & ITEM_SLABBED) == 0);

#ifdef MONTAGE
  PRETIRE(it);
#endif
  do_item_unlink(it, hv);
  return do_item_link(new_it, hv);
}

/*@null@*/
/* This is walking the line of violating lock order, but I think it's safe.
 * If the LRU lock is held, an item in the LRU cannot be wiped and freed.
 * The data could possibly be overwritten, but this is only accessing the
 * headers.
 * It may not be the best idea to leave it like this, but for now it's safe.
 */
char *item_cachedump(const unsigned int slabs_clsid, const unsigned int limit, unsigned int *bytes) {
  unsigned int memlimit = 2 * 1024 * 1024;   /* 2MB max response size */
  char *buffer;
  unsigned int bufcurr;
  item *it;
  unsigned int len;
  unsigned int shown = 0;
  char key_temp[KEY_MAX_LENGTH + 1];
  char temp[512];
  unsigned int id = slabs_clsid;
  id |= COLD_LRU;

  pthread_mutex_lock(&lru_locks[id]);
  it = heads[id];

  buffer = (char*)pm_malloc((size_t)memlimit);
  if (buffer == 0) {
    pthread_mutex_unlock(&lru_locks[id]);
    return NULL;
  }
  bufcurr = 0;

  while (it != NULL && (limit == 0 || shown < limit)) {
    assert(it->nkey <= KEY_MAX_LENGTH);
    if (it->nbytes == 0 && it->nkey == 0) {
      it = it->next;
      continue;
    }
    /* Copy the key since it may not be null-terminated in the struct */
    strncpy(key_temp, ITEM_key(it), it->nkey);
    key_temp[it->nkey] = 0x00; /* terminate */
    len = snprintf(temp, sizeof(temp), "ITEM %s [%d b; %llu s]\r\n",
        key_temp, it->nbytes - 2,
        it->exptime == 0 ? 0 :
        (unsigned long long)it->exptime + process_started);
    if (bufcurr + len + 6 > memlimit)  /* 6 is END\r\n\0 */
      break;
    memcpy(buffer + bufcurr, temp, len);
    bufcurr += len;
    shown++;
    it = it->next;
  }

  memcpy(buffer + bufcurr, "END\r\n", 6);
  bufcurr += 5;

  *bytes = bufcurr;
  pthread_mutex_unlock(&lru_locks[id]);
  return buffer;
}

/* With refactoring of the various stats code the automover won't need a
 * custom function here.
 */
void fill_item_stats_automove(item_stats_automove *am) {
  assert(0 && "changed behavior by removing itemstats");
}

/* I think there's no way for this to be accurate without using the CAS value.
 * Since items getting their time value bumped will pass this validation.
 */

/** dumps out a list of objects of each size, with granularity of 32 bytes */
/*@null@*/
/* Locks are correct based on a technicality. Holds LRU lock while doing the
 * work, so items can't go invalid, and it's only looking at header sizes
 * which don't change.
 */

/** wrapper around assoc_find which does the lazy expiration logic */
item *do_item_get(const char *key, const size_t nkey, const uint32_t hv, const bool do_update) {
  item *it = assoc_find(key, nkey, hv);
  if (it != NULL) {
    refcount_incr(it);
    /* Optimization for slab reassignment. prevents popular items from
     * jamming in busy wait. Can only do this here to satisfy lock order
     * of item_lock, slabs_lock. */
    /* This was made unsafe by removal of the cache_lock:
     * slab_rebalance_signal and slab_rebal.* are modified in a separate
     * thread under slabs_lock. If slab_rebalance_signal = 1, slab_start =
     * NULL (0), but slab_end is still equal to some value, this would end
     * up unlinking every item fetched.
     * This is either an acceptable loss, or if slab_rebalance_signal is
     * true, slab_start/slab_end should be put behind the slabs_lock.
     * Which would cause a huge potential slowdown.
     * Could also use a specific lock for slab_rebal.* and
     * slab_rebalance_signal (shorter lock?)
     */
    /*if (slab_rebalance_signal &&
      ((void *)it >= slab_rebal.slab_start && (void *)it < slab_rebal.slab_end)) {
      do_item_unlink(it, hv);
      do_item_remove(it);
      it = NULL;
      }*/
  }
  if (it != NULL) {
    if (item_is_flushed(it)) {
      do_item_unlink(it, hv);
      do_item_remove(it);
      it = NULL;
    } else if (it->exptime != 0 && it->exptime <= *current_time) {
      do_item_unlink(it, hv);
      do_item_remove(it);
      it = NULL;
    } else {
      if (do_update) {
        do_item_bump(it, hv);
      }
      DEBUG_REFCNT(it, '+');
    }
  }

  return it;
}

// Requires lock held for item.
// Split out of do_item_get() to allow mget functions to look through header
// data before losing state modified via the bump function.
void do_item_bump(item *it, const uint32_t hv) {
  /* We update the hit markers only during fetches.
   * An item needs to be hit twice overall to be considered
   * ACTIVE, but only needs a single hit to maintain activity
   * afterward.
   * FETCHED tells if an item has ever been active.
   */
  it->it_flags |= ITEM_FETCHED;
  do_item_update(it);
}

item *do_item_touch(const char *key, size_t nkey, uint32_t exptime,
    const uint32_t hv) {
  item *it = do_item_get(key, nkey, hv, DO_UPDATE);
  if (it != NULL) {
    it->exptime = exptime;
  }
  return it;
}

/*** LRU MAINTENANCE THREAD ***/

/* Returns number of items remove, expired, or evicted.
 * Callable from worker threads or the LRU maintainer thread */
int lru_pull_tail(const int orig_id, const int cur_lru,
    const uint64_t total_bytes, const uint8_t flags, const rel_time_t max_age,
    struct lru_pull_tail_return *ret_it) {
  item *it = NULL;
  int removed = 0;
  if (orig_id == 0)
    return 0;

  int tries = 5;
  item *search;
  item *next_it;
  void *hold_lock = NULL;
  unsigned int move_to_lru = 0;
  uint64_t limit = 0;

  const int id = orig_id | cur_lru;
  pthread_mutex_lock(&lru_locks[id]);
  search = tails[id];
  /* We walk up *only* for locked items, and if bottom is expired. */
  for (; tries > 0 && search != NULL; tries--, search=next_it) {
    /* we might relink search mid-loop, so search->prev isn't reliable */
    next_it = search->prev;
    if (search->nbytes == 0 && search->nkey == 0 && search->it_flags == 1) {
      /* We are a crawler, ignore it. */
      if (flags & LRU_PULL_CRAWL_BLOCKS) {
        pthread_mutex_unlock(&lru_locks[id]);
        return 0;
      }
      tries++;
      continue;
    }
    uint32_t hv = tcd_hash(ITEM_key(search), search->nkey);
    /* Attempt to tcd_hash item lock the "search" item. If locked, no
     * other callers can incr the refcount. Also skip ourselves. */
    if ((hold_lock = item_trylock(hv)) == NULL)
      continue;
    /* Now see if the item is refcount locked */
    if (refcount_incr(search) != 2) {
      /* Note pathological case with ref'ed items in tail.
       * Can still unlink the item, but it won't be reusable yet */
      /* In case of refcount leaks, enable for quick workaround. */
      /* WARNING: This can cause terrible corruption */
      if (settings.tail_repair_time &&
          search->time + settings.tail_repair_time < *current_time) {
        search->refcount = 1;
        /* This will call item_remove -> item_free since refcnt is 1 */
        do_item_unlink_nolock(search, hv);
        item_trylock_unlock(hold_lock);
        continue;
      }
    }

    /* Expired or flushed */
    if ((search->exptime != 0 && search->exptime < *current_time)
        || item_is_flushed(search)) {
      if ((search->it_flags & ITEM_FETCHED) == 0) {
      }
      /* refcnt 2 -> 1 */
      do_item_unlink_nolock(search, hv);
      /* refcnt 1 -> 0 -> item_free */
      do_item_remove(search);
      item_trylock_unlock(hold_lock);
      removed++;

      /* If all we're finding are expired, can keep going */
      continue;
    }

    /* If we're HOT_LRU or WARM_LRU and over size limit, send to COLD_LRU.
     * If we're COLD_LRU, send to WARM_LRU unless we need to evict
     */
    switch (cur_lru) {
      case HOT_LRU:
        limit = total_bytes * 20 / 100;
      case WARM_LRU:
        if (limit == 0)
          limit = total_bytes * 40 / 100;
        /* Rescue ACTIVE items aggressively */
        if ((search->it_flags & ITEM_ACTIVE) != 0) {
          search->it_flags &= ~ITEM_ACTIVE;
          removed++;
          if (cur_lru == WARM_LRU) {
            do_item_unlink_q(search);
            do_item_link_q(search);
            do_item_remove(search);
            item_trylock_unlock(hold_lock);
          } else {
            /* Active HOT_LRU items flow to WARM */
            move_to_lru = WARM_LRU;
            do_item_unlink_q(search);
            it = search;
          }
        } else if (sizes_bytes[id] > limit ||
            *current_time - search->time > max_age) {
          move_to_lru = COLD_LRU;
          do_item_unlink_q(search);
          it = search;
          removed++;
          break;
        } else {
          /* Don't want to move to COLD, not active, bail out */
          it = search;
        }
        break;
      case COLD_LRU:
        it = search; /* No matter what, we're stopping */
        if (flags & LRU_PULL_EVICT) {
          if (settings.evict_to_free == 0) {
            /* Don't think we need a counter for this. It'll OOM.  */
            break;
          }
          if (search->exptime != 0)
            do_item_unlink_nolock(search, hv);
          removed++;
        } else if (flags & LRU_PULL_RETURN_ITEM) {
          /* Keep a reference to this item and return it. */
          ret_it->it = it;
          ret_it->hv = hv;
        } 
        break;
      case TEMP_LRU:
        it = search; /* Kill the loop. Parent only interested in reclaims */
        break;
    }
    if (it != NULL)
      break;
  }

  pthread_mutex_unlock(&lru_locks[id]);

  if (it != NULL) {
    if (move_to_lru) {
      it->slabs_clsid = ITEM_clsid(it);
      it->slabs_clsid |= move_to_lru;
      item_link_q(it);
    }
    if ((flags & LRU_PULL_RETURN_ITEM) == 0) {
      do_item_remove(it);
      item_trylock_unlock(hold_lock);
    }
  }

  return removed;
}


/* TODO: Third place this code needs to be deduped */
static void lru_bump_buf_link_q(lru_bump_buf *b) {
  pthread_mutex_lock(&bump_buf_lock);
  assert(b != bump_buf_head);

  b->prev = 0;
  b->next = bump_buf_head;
  if (b->next) b->next->prev = b;
  bump_buf_head = b;
  if (bump_buf_tail == 0) bump_buf_tail = b;
  pthread_mutex_unlock(&bump_buf_lock);
  return;
}

void *item_lru_bump_buf_create(void) {
  lru_bump_buf *b = (lru_bump_buf*)pm_calloc(1, sizeof(lru_bump_buf));
  if (b == NULL) {
    return NULL;
  }

  b->buf = bipbuf_new(sizeof(lru_bump_entry) * LRU_BUMP_BUF_SIZE);
  if (b->buf == NULL) {
    free(b);
    return NULL;
  }

  pthread_mutex_init(&b->mutex, NULL);

  lru_bump_buf_link_q(b);
  return b;
}

/* TODO: Might be worth a micro-optimization of having bump buffers link
 * themselves back into the central queue when queue goes from zero to
 * non-zero, then remove from list if zero more than N times.
 * If very few hits on cold this would avoid extra memory barriers from LRU
 * maintainer thread. If many hits, they'll just stay in the list.
 */

/* Loop up to N times:
 * If too many items are in HOT_LRU, push to COLD_LRU
 * If too many items are in WARM_LRU, push to COLD_LRU
 * If too many items are in COLD_LRU, poke COLD_LRU tail
 * 1000 loops with 1ms min sleep gives us under 1m items shifted/sec. The
 * locks can't handle much more than that. Leaving a TODO for how to
 * autoadjust in the future.
 */
static int lru_maintainer_juggle(const int slabs_clsid) {
  int i;
  int did_moves = 0;
  uint64_t total_bytes = 0;
  if (settings.temp_lru) {
    // Only looking for reclaims. Run before we size the LRU. 
    for (i = 0; i < 500; i++) {
      if (lru_pull_tail(slabs_clsid, TEMP_LRU, 0, 0, 0, NULL) <= 0) {
        break;
      } else {
        did_moves++;
      }
    }
  }

  rel_time_t hot_age = 0;
  rel_time_t warm_age = 0;
  // If LRU is in flat mode, force items to drain into COLD via max age of 0 
  // Juggle HOT/WARM up to N times 
  for (i = 0; i < 500; i++) {
    int do_more = 0;
    if (lru_pull_tail(slabs_clsid, HOT_LRU, total_bytes, LRU_PULL_CRAWL_BLOCKS, hot_age, NULL) ||
        lru_pull_tail(slabs_clsid, WARM_LRU, total_bytes, LRU_PULL_CRAWL_BLOCKS, warm_age, NULL)) {
      do_more++;
    }
    if (do_more == 0)
      break;
    did_moves++;
  }
  return did_moves;
}

// Will crawl all slab classes a minimum of once per hour
#define MAX_MAINTCRAWL_WAIT 60 * 60

/* Hoping user input will improve this function. This is all a wild guess.
 * Operation: Kicks crawler for each slab id. Crawlers take some statistics as
 * to items with nonzero expirations. It then buckets how many items will
 * expire per minute for the next hour.
 * This function checks the results of a run, and if it things more than 1% of
 * expirable objects are ready to go, kick the crawler again to reap.
 * It will also kick the crawler once per minute regardless, waiting a minute
 * longer for each time it has no work to do, up to an hour wait time.
 * The latter is to avoid newly started daemons from waiting too long before
 * retrying a crawl.
 */
static void lru_maintainer_crawler_check(struct crawler_expired_data *cdata) {
  int i;
  static rel_time_t next_crawls[POWER_LARGEST];
  static rel_time_t next_crawl_wait[POWER_LARGEST];
  uint8_t todo[POWER_LARGEST];
  memset(todo, 0, sizeof(uint8_t) * POWER_LARGEST);
  bool do_run = false;
  unsigned int tocrawl_limit = 0;

  // TODO: If not segmented LRU, skip non-cold
  for (i = POWER_SMALLEST; i < POWER_LARGEST; i++) {
    crawlerstats_t *s = &cdata->crawlerstats[i];
    // We've not successfully kicked off a crawl yet. 
    if (s->run_complete) {
      pthread_mutex_lock(&cdata->lock);
      int x;
      // Should we crawl again? 
      uint64_t possible_reclaims = s->seen - s->noexp;
      uint64_t available_reclaims = 0;
      // Need to think we can free at least 1% of the items before
      // crawling.
      uint64_t low_watermark = (possible_reclaims / 100) + 1;
      // Don't bother if the payoff is too low. 
      for (x = 0; x < 60; x++) {
        available_reclaims += s->histo[x];
        if (available_reclaims > low_watermark) {
          if (next_crawl_wait[i] < (x * 60)) {
            next_crawl_wait[i] += 60;
          } else if (next_crawl_wait[i] >= 60) {
            next_crawl_wait[i] -= 60;
          }
          break;
        }
      }

      if (available_reclaims == 0) {
        next_crawl_wait[i] += 60;
      }

      if (next_crawl_wait[i] > MAX_MAINTCRAWL_WAIT) {
        next_crawl_wait[i] = MAX_MAINTCRAWL_WAIT;
      }

      next_crawls[i] = *current_time + next_crawl_wait[i] + 5;
      // Got our calculation, avoid running until next actual run.
      s->run_complete = false;
      pthread_mutex_unlock(&cdata->lock);
    }
    if (*current_time > next_crawls[i]) {
      pthread_mutex_lock(&lru_locks[i]);
      if (sizes[i] > tocrawl_limit) {
        tocrawl_limit = sizes[i];
      }
      pthread_mutex_unlock(&lru_locks[i]);
      todo[i] = 1;
      do_run = true;
      next_crawls[i] = *current_time + 5; // minimum retry wait.
    }
  }
  if (do_run) {
    if (settings.lru_crawler_tocrawl && settings.lru_crawler_tocrawl < tocrawl_limit) {
      tocrawl_limit = settings.lru_crawler_tocrawl;
    }
    lru_crawler_start(todo, tocrawl_limit, CRAWLER_AUTOEXPIRE, cdata, NULL, 0);
  }
}

static pthread_t lru_maintainer_tid;

#define MAX_LRU_MAINTAINER_SLEEP 1000000
#define MIN_LRU_MAINTAINER_SLEEP 1000

static void *lru_maintainer_thread(void *arg) {
//  slab_automove_reg_t *sam = &slab_automove_default;
  int i;
  useconds_t to_sleep = MIN_LRU_MAINTAINER_SLEEP;
  useconds_t last_sleep = MIN_LRU_MAINTAINER_SLEEP;
  rel_time_t last_crawler_check = 0;
//  rel_time_t last_automove_check = 0;
  useconds_t next_juggles[MAX_NUMBER_OF_SLAB_CLASSES] = {0};
  useconds_t backoff_juggles[MAX_NUMBER_OF_SLAB_CLASSES] = {0};
  crawler_expired_data *cdata = (crawler_expired_data*)
    pm_calloc(1, sizeof(struct crawler_expired_data));
  if (cdata == NULL) {
    fprintf(stderr, "Failed to allocate crawler data for LRU maintainer thread\n");
    abort();
  }
  pthread_mutex_init(&cdata->lock, NULL);
  cdata->crawl_complete = true; // kick off the crawler.

//  double last_ratio = settings.slab_automove_ratio;
//  void *am = sam->init(&settings);

  pthread_mutex_lock(lru_maintainer_lock);
  while (do_run_lru_maintainer_thread) {
    pthread_mutex_unlock(lru_maintainer_lock);
    if (to_sleep)
      usleep(to_sleep);
    pthread_mutex_lock(lru_maintainer_lock);
    // A sleep of zero counts as a minimum of a 1ms wait 
    last_sleep = to_sleep > 1000 ? to_sleep : 1000;
    to_sleep = MAX_LRU_MAINTAINER_SLEEP;
    STATS_LOCK();
    stats->lru_maintainer_juggles++;
    STATS_UNLOCK();

    // Each slab class gets its own sleep to avoid hammering locks 
    for (i = POWER_SMALLEST; i < MAX_NUMBER_OF_SLAB_CLASSES; i++) {
      next_juggles[i] = next_juggles[i] > last_sleep ? next_juggles[i] - last_sleep : 0;

      if (next_juggles[i] > 0) {
        // Sleep the thread just for the minimum amount (or not at all)
        if (next_juggles[i] < to_sleep)
          to_sleep = next_juggles[i];
        continue;
      }

      int did_moves = lru_maintainer_juggle(i);
      if (did_moves == 0) {
        if (backoff_juggles[i] != 0) {
          backoff_juggles[i] += backoff_juggles[i] / 8;
        } else {
          backoff_juggles[i] = MIN_LRU_MAINTAINER_SLEEP;
        }
        if (backoff_juggles[i] > MAX_LRU_MAINTAINER_SLEEP)
          backoff_juggles[i] = MAX_LRU_MAINTAINER_SLEEP;
      } else if (backoff_juggles[i] > 0) {
        backoff_juggles[i] /= 2;
        if (backoff_juggles[i] < MIN_LRU_MAINTAINER_SLEEP) {
          backoff_juggles[i] = 0;
        }
      }
      next_juggles[i] = backoff_juggles[i];
      if (next_juggles[i] < to_sleep)
        to_sleep = next_juggles[i];
    }

    // Once per second at most 
    if (settings.lru_crawler && last_crawler_check != *current_time) {
      lru_maintainer_crawler_check(cdata);
      last_crawler_check = *current_time;
    }

    /*
     * No longer using slab allocator so we don't need to automove
    if (last_automove_check != *current_time) {
      if (last_ratio != settings.slab_automove_ratio) {
        sam->free(am);
        am = sam->init(&settings);
        last_ratio = settings.slab_automove_ratio;
      }
      int src, dst;
      sam->run(am, &src, &dst);
      if (src != -1 && dst != -1) {
        slabs_reassign(src, dst);
      }
      // dst == 0 means reclaim to global pool, be more aggressive
      if (dst != 0) {
        last_automove_check = current_time;
      } else if (dst == 0) {
        // also ensure we minimize the thread sleep
        to_sleep = 1000;
      }
    }
    */
  }
  pthread_mutex_unlock(lru_maintainer_lock);
  // sam->free(am);
  // LRU crawler *must* be stopped.
  free(cdata);
  return NULL;
}

int stop_lru_maintainer_thread(void) {
  int ret;
  pthread_mutex_lock(lru_maintainer_lock);
  // LRU thread is a sleep loop, will die on its own 
  do_run_lru_maintainer_thread = 0;
  pthread_mutex_unlock(lru_maintainer_lock);
  if ((ret = pthread_join(lru_maintainer_tid, NULL)) != 0) {
    fprintf(stderr, "Failed to stop LRU maintainer thread: %s\n", strerror(ret));
    return -1;
  }
  settings.lru_maintainer_thread = false;
  return 0;
}

int start_lru_maintainer_thread(void *arg) {
  int ret;

  pthread_mutex_lock(lru_maintainer_lock);
  do_run_lru_maintainer_thread = 1;
  settings.lru_maintainer_thread = true;
  if ((ret = pthread_create(&lru_maintainer_tid, NULL,
          lru_maintainer_thread, arg)) != 0) {
    fprintf(stderr, "Can't create LRU maintainer thread: %s\n",
        strerror(ret));
    pthread_mutex_unlock(lru_maintainer_lock);
    return -1;
  }
  pthread_mutex_unlock(lru_maintainer_lock);

  return 0;
}

// If we hold this lock, crawler can't wake up or move 
void lru_maintainer_pause(void) {
  pthread_mutex_lock(lru_maintainer_lock);
}

void lru_maintainer_resume(void) {
  pthread_mutex_unlock(lru_maintainer_lock);
}

int init_lru_maintainer(void) {
  lru_maintainer_initialized = 1;
  return 0;
}

/* Tail linkers and crawler for the LRU crawler. */
void do_item_linktail_q(item *it) { /* item is the new tail */
  pptr<item> *head, *tail;
  assert(it->it_flags == 1);
  assert(it->nbytes == 0);

  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];
  //assert(*tail != 0);
  assert(it != *tail);
  assert((*head != NULL && *tail != NULL) || (*head == 0 && *tail == 0));
  it->prev = *tail;
  it->next = 0;
  if (it->prev != NULL) {
    assert(it->prev->next == 0);
    it->prev->next = it;
  }
  *tail = it;
  if (*head == 0) *head = it;
  return;
}

void do_item_unlinktail_q(item *it) {
  pptr<item> *head, *tail;
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  if (*head == it) {
    assert(it->prev == 0);
    *head = it->next;
  }
  if (*tail == it) {
    assert(it->next == 0);
    *tail = it->prev;
  }
  assert(it->next != it);
  assert(it->prev != it);

  if (it->next != NULL) it->next->prev = it->prev;
  if (it->prev != NULL) it->prev->next = it->next;
  return;
}

/* This is too convoluted, but it's a difficult shuffle. Try to rewrite it
 * more clearly. */
item *do_item_crawl_q(item *it) {
  pptr<item> *head, *tail;
  assert(it->it_flags == 1);
  assert(it->nbytes == 0);
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  /* We've hit the head, pop off */
  if (it->prev == 0) {
    assert(*head == it);
    if (it->next != NULL) {
      *head = it->next;
      assert(it->next->prev == it);
      it->next->prev = 0;
    }
    return NULL; /* Done */
  }

  /* Swing ourselves in front of the next item */
  /* NB: If there is a prev, we can't be the head */
  assert(it->prev != it);
  if (it->prev != NULL) {
    if (*head == it->prev) {
      /* Prev was the head, now we're the head */
      *head = it;
    }
    if (*tail == it) {
      /* We are the tail, now they are the tail */
      *tail = it->prev;
    }
    assert(it->next != it);
    if (it->next != NULL) {
      assert(it->prev->next == it);
      it->prev->next = it->next;
      it->next->prev = it->prev;
    } else {
      /* Tail. Move this above? */
      it->prev->next = 0;
    }
    /* prev->prev's next is it->prev */
    it->next = it->prev;
    it->prev = it->next->prev;
    it->next->prev = it;
    /* New it->prev now, if we're not at the head. */
    if (it->prev != NULL) {
      it->prev->next = it;
    }
  }
  assert(it->next != it);
  assert(it->prev != it);

  return it->next; /* success */
}

/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Thread management for memcached.
 */
#include "memcached.h"
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define ITEMS_PER_ALLOC 64
 
// The following sets of locks ~probably~ don't have to be persistent, but they
// DO have to be shared among processes, which mean they have to live in
// rpmalloc

/* Locks for cache LRU operations */
pthread_mutex_t* lru_locks;

/* Lock for global stats */
pthread_mutex_t* stats_lock;

pthread_mutex_t *item_locks;
/* size of the item lock hash table */
static uint32_t item_lock_count;
unsigned int item_lock_hashpower = 11;
#define hashsize(n) ((unsigned long int)1<<(n))
#define hashmask(n) (hashsize(n)-1)

/* item_lock() must be held for an item before any modifications to either its
 * associated hash bucket, or the structure itself.
 * LRU modifications must hold the item lock, and the LRU lock.
 * LRU's accessing items must item_trylock() before modifying an item.
 * Items accessible from an LRU must not be freed or modified
 * without first locking and removing from the LRU.
 */

void item_lock(uint32_t hv) {
  volatile unsigned q = hv & hashmask(item_lock_hashpower);
  mutex_lock(&item_locks[q]);
}

void *item_trylock(uint32_t hv) {
  pthread_mutex_t *lock = &item_locks[hv & hashmask(item_lock_hashpower)];
  if (pthread_mutex_trylock(lock) == 0) {
    return lock;
  }
  return NULL;
}

void item_trylock_unlock(void *lock) {
  mutex_unlock((pthread_mutex_t *) lock);
}

void item_unlock(uint32_t hv) {
  mutex_unlock(&item_locks[hv & hashmask(item_lock_hashpower)]);
}


/*
 * Initializes the thread subsystem, creating various worker threads.
 */
void memcached_thread_init() {
  item_lock_count = hashsize(item_lock_hashpower);
  if (is_restart){
    item_locks = pm_get_root<pthread_mutex_t>(RPMRoot::ItemLocks);
    stats_lock = pm_get_root<pthread_mutex_t>(RPMRoot::StatLock);
    lru_locks = pm_get_root<pthread_mutex_t>(RPMRoot::LRULocks);
  } else {
    lru_locks = (pthread_mutex_t*)pm_malloc(sizeof(pthread_mutex_t)*POWER_LARGEST);
    item_locks = (pthread_mutex_t*)pm_calloc(item_lock_count, sizeof(pthread_mutex_t));
    stats_lock = (pthread_mutex_t*)pm_malloc(sizeof(pthread_mutex_t));

    assert(item_locks != nullptr);
    assert(stats_lock != nullptr);
    assert(lru_locks  != nullptr);

    pm_set_root(item_locks, RPMRoot::ItemLocks);
    pm_set_root(stats_lock, RPMRoot::StatLock);
    pm_set_root(lru_locks, RPMRoot::LRULocks);
    
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // the mutex may be shared across processes
    pthread_mutexattr_setpshared(&attr, 1);
    for (unsigned i = 0; i < POWER_LARGEST; ++i){
      pthread_mutex_init(&lru_locks[i], &attr);
      pthread_mutex_init(&item_locks[i], &attr);
    }
    pthread_mutex_init(stats_lock, &attr);
  }
}

/********************************* ITEM ACCESS *******************************/

/*
 * Allocates a new item.
 */
item *item_alloc(const char * key, size_t nkey, int flags, rel_time_t exptime, int nbytes) {
  item *it;
  /* do_item_alloc handles its own locks */
  it = do_item_alloc(key, nkey, flags, exptime, nbytes);
  return it;
}

/*
 * Returns an item if it hasn't been marked as expired,
 * lazy-expiring as needed.
 */
item *item_get(const char *key, const size_t nkey, const bool do_update) {
  item *it;
  uint32_t hv;
  hv = tcd_hash(key, nkey);
  item_lock(hv);
  it = do_item_get(key, nkey, hv, do_update);
  item_unlock(hv);
  return it;
}

// returns an item with the item lock held.
// lock will still be held even if return is NULL, allowing caller to replace
// an item atomically if desired.
item *item_get_locked(const char *key, const size_t nkey, const bool do_update, uint32_t *hv) {
    *hv = tcd_hash(key, nkey);
    item_lock(*hv);
    return  do_item_get(key, nkey, *hv, do_update);
}

item *item_touch(const char *key, size_t nkey, uint32_t exptime) {
  item *it;
  uint32_t hv;
  hv = tcd_hash(key, nkey);
  item_lock(hv);
  it = do_item_touch(key, nkey, exptime, hv);
  item_unlock(hv);
  return it;
}

/*
 * Links an item into the LRU and hashtable.
 */
int item_link(item *item) {
  int ret;
  uint32_t hv;

  hv = tcd_hash(ITEM_key(item), item->nkey);
  item_lock(hv);
  ret = do_item_link(item, hv);
  item_unlock(hv);
  return ret;
}

/*
 * Decrements the reference count on an item and adds it to the freelist if
 * needed.
 */
void item_remove(item *item) {
  uint32_t hv;
  hv = tcd_hash(ITEM_key(item), item->nkey);

  item_lock(hv);
  do_item_remove(item);
  item_unlock(hv);
}

/*
 * Replaces one item with another in the hashtable.
 * Unprotected by a mutex lock since the core server does not require
 * it to be thread-safe.
 */
int item_replace(item *old_it, item *new_it, const uint32_t hv) {
  return do_item_replace(old_it, new_it, hv);
}

/*
 * Unlinks an item from the LRU and hashtable.
 */
void item_unlink(item *item) {
  uint32_t hv;
  hv = tcd_hash(ITEM_key(item), item->nkey);
  item_lock(hv);
  do_item_unlink(item, hv);
  item_unlock(hv);
}

/*
 * Does arithmetic on a numeric item value.
 */
enum delta_result_type add_delta(const char *key,
                                 const size_t nkey, bool incr,
                                 const uint64_t delta, uint64_t *value) {
    enum delta_result_type ret;
    uint32_t hv;

    hv = tcd_hash(key, nkey);
    item_lock(hv);
    ret = do_add_delta(key, nkey, incr, delta, value, hv);
    item_unlock(hv);
    return ret;
}

/*
 * Stores an item in the cache (high level, obeys set/add/replace semantics)
 */
enum store_item_type store_item(item *item, int comm) {
  enum store_item_type ret;
  uint32_t hv;

  hv = tcd_hash(ITEM_key(item), item->nkey);
  item_lock(hv);
#ifdef MONTAGE
  BEGIN_OP();
#endif
  ret = do_store_item(item, comm, hv).first;
#ifdef MONTAGE
  // Wentao: PMALLOC is used to allocate new items, so we don't
  // explicitly register allocated blocks anymore
  END_OP;
#endif
  item_unlock(hv);
  return ret;
}

/******************************* GLOBAL STATS ******************************/

void STATS_LOCK() {
    pthread_mutex_lock(stats_lock);
}

void STATS_UNLOCK() {
    pthread_mutex_unlock(stats_lock);
}

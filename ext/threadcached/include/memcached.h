/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */

/** \file
 * The main memcached header holding commonly used data
 * structures and function prototypes.
 */
#ifndef MEMCACHED_H
#define MEMCACHED_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>
#include <event.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <string>

#include <util.h>

#include <pptr.hpp>

// THREADCACHED
using rel_time_t = int;
#include "murmur3_hash.h"
#include "itoa_ljust.h"
#include <atomic>
#include <AllocatorMacro.hpp>
// #include <BaseMeta.hpp>
#define tcd_hash MurmurHash3_x86_32
/* RPMalloc Root IDs */
#define MEMCACHED_BITFIELD :1
enum RPMRoot {
  PrimaryHT = 0,
  OldHT = 1,
  StatLock = 2,
  ItemLocks = 3,
  LRULocks = 4,
  Heads = 5,
  Tails = 6,
  ItemStats = 7,
  Sizes = 8,
  SizesBytes = 9,
  SlabclassAr = 10,
  MemLimit = 11,
  EndSignal = 12,
  CTime = 13,
  SlabLock = 14,
  PSig = 15,
  NLookers = 16,
  TStats = 17,
  GStats = 18,
  Stats = 19,
  LRUMaintainerLock = 20,
};
extern int is_server;
extern int is_restart;
// 70GB
const size_t MEMORY_MAX = 70*1024*1024*1024ULL;

/** Maximum length of a key. */
#define KEY_MAX_LENGTH 250

/** Size of an incr buf. */
#define INCR_MAX_STORAGE_LEN 24

#define DATA_BUFFER_SIZE 2048
#define UDP_READ_BUFFER_SIZE 65536
#define UDP_MAX_PAYLOAD_SIZE 1400
#define UDP_HEADER_SIZE 8
#define MAX_SENDBUF_SIZE (256 * 1024 * 1024)
/* Up to 3 numbers (2 32bit, 1 64bit), spaces, newlines, null 0 */
#define SUFFIX_SIZE 50

/** Initial size of list of items being returned by "get". */
#define ITEM_LIST_INITIAL 200

/** Initial size of list of CAS suffixes appended to "gets" lines. */
#define SUFFIX_LIST_INITIAL 100

/** Initial size of the sendmsg() scatter/gather array. */
#define IOV_LIST_INITIAL 400

/** Initial number of sendmsg() argument structures to allocate. */
#define MSG_LIST_INITIAL 10

/** High water marks for buffer shrinking */
#define READ_BUFFER_HIGHWAT 8192
#define ITEM_LIST_HIGHWAT 400
#define IOV_LIST_HIGHWAT 600
#define MSG_LIST_HIGHWAT 100

/* Binary protocol stuff */
#define MIN_BIN_PKT_LENGTH 16
#define BIN_PKT_HDR_WORDS (MIN_BIN_PKT_LENGTH/sizeof(uint32_t))

/* Initial power multiplier for the hash table */
#define HASHPOWER_DEFAULT 25
#define HASHPOWER_MAX 32

/*
 * We only reposition items in the LRU queue if they haven't been repositioned
 * in this many seconds. That saves us from churning on frequently-accessed
 * items.
 */
#define ITEM_UPDATE_INTERVAL 60

/* unistd.h is here */
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

/* Slab sizing definitions. */
#define POWER_SMALLEST 1
#define POWER_LARGEST  256 /* actual cap is 255 */
#define SLAB_GLOBAL_PAGE_POOL 0 /* magic slab class for storing pages for reassignment */
#define CHUNK_ALIGN_BYTES 8
/* slab class max is a 6-bit number, -1. */
#define MAX_NUMBER_OF_SLAB_CLASSES (63 + 1)

/** How long an object can reasonably be assumed to be locked before
  harvesting it on a low memory condition. Default: disabled. */
#define TAIL_REPAIR_TIME_DEFAULT 0

/* warning: don't use these macros with a function, as it evals its arg twice */
#define ITEM_get_cas(i) (((i)->it_flags & ITEM_CAS) ? \
    (i)->data->cas : (uint64_t)0)

#define ITEM_set_cas(i,v) { \
  if ((i)->it_flags & ITEM_CAS) { \
    (i)->data->cas = v; \
  } \
}

#define ITEM_key(it) (((char*)&((it)->data)) \
    + (((it)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

#define ITEM_suffix(it) ((char*) &((it)->data) + (it)->nkey + 1 \
    + (((it)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

#define ITEM_data(it) ((char*) &((it)->data) + (it)->nkey + 1 \
    + (it)->nsuffix \
    + (((it)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

#define ITEM_ntotal(it) (sizeof(item) + (it)->nkey + 1 \
    + (it)->nsuffix + (it)->nbytes \
    + (((it)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

#define ITEM_clsid(it) ((it)->slabs_clsid & ~(3<<6))
#define ITEM_lruid(it) ((it)->slabs_clsid & (3<<6))

#define STAT_KEY_LEN 128
#define STAT_VAL_LEN 128
/** Item client flag conversion */
#define FLAGS_CONV(iar, it, flag) { \
  if ((iar)) { \
    flag = (uint32_t) strtoul(ITEM_suffix((it)), (char **) NULL, 10); \
  } else if ((it)->nsuffix > 0) { \
    flag = *((uint32_t *)ITEM_suffix((it))); \
  } else { \
    flag = 0; \
  } \
}

struct pthread_args {
  int argc;
  char **argv;
};

#include "pku_memcached.h"
#include "constants.h"

void pause_accesses(void);
void unpause_accesses(void);

struct item;

memcached_return_t
pku_memcached_get(const char* key, size_t nkey, char* &buffer, size_t *buffLen,
    uint32_t *flags);
memcached_return_t
pku_memcached_mget(const char * const *keys, const size_t *key_length,
   size_t number_of_keys, item **list);
memcached_return_t
pku_memcached_insert(const char* key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime);
memcached_return_t
pku_memcached_set(const char *key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime);
memcached_return_t
pku_memcached_delete(const char *key, size_t nkey, uint32_t exptime);
memcached_return_t
pku_memcached_flush(uint32_t exptime);
memcached_return_t
pku_memcached_replace(const char *key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags);
memcached_return_t
pku_memcached_prepend(const char *key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags);
memcached_return_t
pku_memcached_append(const char *key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags);

/*
 * NOTE: If you modify this table you _MUST_ update the function state_text
 */
enum pause_thread_types {
  PAUSE_WORKER_THREADS = 0,
  PAUSE_ALL_THREADS,
  RESUME_ALL_THREADS,
  RESUME_WORKER_THREADS
};

#define IS_TCP(x) (x == tcp_transport)
#define IS_UDP(x) (x == udp_transport)

#define NREAD_ADD 1
#define NREAD_SET 2
#define NREAD_REPLACE 3
#define NREAD_APPEND 4
#define NREAD_PREPEND 5
#define NREAD_CAS 6

enum store_item_type {
  NOT_STORED=0, STORED, EXISTS, NOT_FOUND, TOO_LARGE, NO_MEMORY
};

/** Time relative to server start. Smaller than time_t on 64-bit systems. */
// TODO: Move to sub-header. needed in logger.h
//typedef unsigned int rel_time_t;

/** Use X macros to avoid iterating over the stats fields during reset and
 * aggregation. No longer have to add new stats in 3+ places.
 */


/** Stats stored per slab (and per thread). */
struct slab_stats {
  std::atomic<uint64_t> set_cmds;
  std::atomic<uint64_t> get_hits;
  std::atomic<uint64_t> touch_hits;
  std::atomic<uint64_t> delete_hits;
  std::atomic<uint64_t> cas_hits;
  std::atomic<uint64_t> cas_badval;
  std::atomic<uint64_t> incr_hits;
  std::atomic<uint64_t> decr_hits;

};

/**
 * Stats stored per-thread.
 */
struct thread_stats {
  // Combine thread stats into stats_state as well and do a kind of fused
  // approach now that we don't have connections
  std::atomic<uint64_t> get_cmds; 
  std::atomic<uint64_t> get_misses; 
  std::atomic<uint64_t> get_expired; 
  std::atomic<uint64_t> get_flushed; 
  std::atomic<uint64_t> touch_cmds; 
  std::atomic<uint64_t> touch_misses; 
  std::atomic<uint64_t> delete_misses; 
  std::atomic<uint64_t> incr_misses; 
  std::atomic<uint64_t> decr_misses; 
  std::atomic<uint64_t> cas_misses; 
  std::atomic<uint64_t> meta_cmds; 
  std::atomic<uint64_t> bytes_read; 
  std::atomic<uint64_t> bytes_written; 
  std::atomic<uint64_t> flush_cmds; 
  std::atomic<uint64_t> conn_yields; /* # of yields for connections (-R option)*/ 
  std::atomic<uint64_t> auth_cmds; 
  std::atomic<uint64_t> auth_errors; 
  std::atomic<uint64_t> idle_kicks; /* idle connections killed */ 
  std::atomic<uint64_t> response_obj_oom; 
  std::atomic<uint64_t> read_buf_oom;
  struct slab_stats slab_stats[MAX_NUMBER_OF_SLAB_CLASSES];
  std::atomic<uint64_t> lru_hits[POWER_LARGEST];
  std::atomic<uint64_t>       curr_items;
  std::atomic<uint64_t>       curr_bytes;
  std::atomic<uint64_t>       total_items;
};

/**
 * Global stats. Only resettable stats should go into this structure.
 */
struct stats {
  uint64_t      total_conns;
  uint64_t      rejected_conns;
  uint64_t      malloc_fails;
  uint64_t      listen_disabled_num;
  uint64_t      slabs_moved;       /* times slabs were moved around */
  uint64_t      slab_reassign_rescues; /* items rescued during slab move */
  uint64_t      slab_reassign_evictions_nomem; /* valid items lost during slab move */
  uint64_t      slab_reassign_inline_reclaim; /* valid items lost during slab move */
  uint64_t      slab_reassign_chunk_rescues; /* chunked-item chunks recovered */
  uint64_t      slab_reassign_busy_items; /* valid temporarily unmovable */
  uint64_t      slab_reassign_busy_deletes; /* refcounted items killed */
  uint64_t      lru_crawler_starts; /* Number of item crawlers kicked off */
  uint64_t      lru_maintainer_juggles; /* number of LRU bg pokes */
  uint64_t      time_in_listen_disabled_us;  /* elapsed time in microseconds while server unable to process new connections */
  uint64_t      log_worker_dropped; /* logs dropped by worker threads */
  uint64_t      log_worker_written; /* logs written by worker threads */
  uint64_t      log_watcher_skipped; /* logs watchers missed */
  uint64_t      log_watcher_sent; /* logs sent to watcher buffers */
  struct timeval maxconns_entered;  /* last time maxconns entered */
};

/**
 * Global "state" stats. Reflects state that shouldn't be wiped ever.
 * Ordered for some cache line locality for commonly updated counters.
 */

struct stats_state {
  std::atomic<uint64_t>       curr_conns;
  std::atomic<uint64_t>       hash_bytes;       /* size used for hash tables */
  std::atomic<unsigned int>   conn_structs;
  std::atomic<unsigned int>   reserved_fds;
  unsigned int                hash_power_level; /* Better hope it's not over 9000 */
  bool                        hash_is_expanding; /* If the hash table is being expanded */
  // std::atomic<bool>           accepting_conns;   whether we are currently accepting 
  bool                        slab_reassign_running; /* slab reassign in progress */
  bool                        lru_crawler_running; /* crawl in progress */

};

#define MAX_VERBOSITY_LEVEL 2

/* When adding a setting, be sure to update process_stat_settings */
/**
 * Globally accessible settings as derived from the commandline.
 */
struct settings {
  size_t maxbytes;
  rel_time_t oldest_live; /* ignore existing items older than this */
  uint64_t oldest_cas; /* ignore existing items with CAS values lower than this */
  int evict_to_free;
  int access;  /* access mask (a la chmod) for unix domain socket */
  double factor;          /* chunk size growth factor */
  int chunk_size;
  char prefix_delimiter;  /* character that marks a key prefix (for stats) */
  int detail_enabled;     /* nonzero if we're collecting detailed stats */
  int reqs_per_event;     /* Maximum number of io to process on each
                             io-event. */
  int backlog;
  int item_size_max;        /* Maximum item size */
  int slab_chunk_size_max;  /* Upper end for chunks within slab pages. */
  int slab_page_size;     /* Slab's page units. */
  bool maxconns_fast;     /* Whether or not to early close connections */
  bool lru_crawler;        /* Whether or not to enable the autocrawler thread */
  bool lru_maintainer_thread; /* LRU maintainer background thread */
  //  bool slab_reassign always true. Whether or not slab reassignment is allowed
  double slab_automove_ratio; /* youngest must be within pct of oldest */
  unsigned int slab_automove_window; /* window mover for algorithm */
  int hashpower_init;     /* Starting hash power level */
  bool shutdown_command; /* allow shutdown command */
  int tail_repair_time;   /* LRU tail refcount leak repair time */
  bool flush_enabled;     /* flush_all enabled */
  bool dump_enabled;      /* whether cachedump/metadump commands work */
  std::string hash_algorithm;     /* Hash algorithm in use */
  int lru_crawler_sleep;  /* Microsecond sleep between items */
  uint32_t lru_crawler_tocrawl; /* Number of items to crawl per run */
  double hot_max_factor; /* HOT tail age relative to COLD tail */
  double warm_max_factor; /* WARM tail age relative to COLD tail */
  int crawls_persleep; /* Number of LRU crawls to run before sleeping */
  bool inline_ascii_response; /* pre-format the VALUE line for ASCII responses */
  bool temp_lru; /* TTL < temporary_ttl uses TEMP_LRU */
  uint32_t temporary_ttl; /* temporary LRU threshold */
  int idle_timeout;       /* Number of seconds to let connections idle */
  bool relaxed_privileges;   /* Relax process restrictions when running testapp */
};

extern struct stats * stats;
extern struct stats_state *__global_stats;
extern struct thread_stats *__thread_stats;
#define NUM_STATS 64
#define STATS_HASH 63
extern time_t process_started;
extern struct settings settings;
// each instance of memcached will use a different stats_state to reduce the
// amount of trashing
extern unsigned stats_id;
#define ITEM_LINKED 1
#define ITEM_CAS 2

// Item is stored in a slab freelist
#define ITEM_SLABBED 4

/* Item was fetched at least once in its lifetime */
#define ITEM_FETCHED 8
/* Appended on fetch, removed on LRU shuffling */
#define ITEM_ACTIVE 16
/* If an item's storage are chained chunks. */
// This happens if |item| > size_slab_page / 2

/**
 * Structure for storing items within memcached.
 */
#ifdef MONTAGE
#include "montage_global_api.hpp"

using namespace pds;
struct item : public PBlk{
  item(){};
  item(const item& oth): PBlk(oth){};
#else
struct item{
#endif
  /* Protected by LRU locks */
  pptr<item>      next;
  pptr<item>      prev;
  /* Rest are protected by an item lock */
  pptr<item>      h_next;    /* hash chain next */
  rel_time_t      time;       /* least recent access */
  rel_time_t      exptime;    /* expire time */
  int             nbytes;     /* size of data */
  unsigned short  refcount;
  uint8_t         nsuffix;    /* length of flags-and-length string */
  uint8_t         it_flags=0;   /* ITEM_* above */
  uint8_t         slabs_clsid;/* which slab class we're in */
  uint8_t         nkey;       /* key length, w/terminating null and padding */
  /* this odd type prevents type-punning issues when we do
   * the little shuffle to save space when not using CAS. */
  union {
    uint64_t cas;
    char end;
  } data[];
  /* if it_flags & ITEM_CAS we have 8 bytes CAS */
  /* then null-terminated key */
  /* then " flags length\r\n" (no terminating null) */
  /* then data with terminating \r\n (no terminating null; it's binary!) */
};

// TODO: If we eventually want user loaded modules, we can't use an enum :(
enum crawler_run_type {
  CRAWLER_AUTOEXPIRE=0, CRAWLER_EXPIRED, CRAWLER_METADUMP
};

struct crawler{
  item *next;
  item *prev;
  item *h_next;    /* hash chain next */
  rel_time_t      time;       /* least recent access */
  rel_time_t      exptime;    /* expire time */
  int             nbytes;     /* size of data */
  unsigned short  refcount;
  uint8_t         nsuffix;    /* length of flags-and-length string */
  uint8_t         it_flags;   /* ITEM_* above */
  uint8_t         slabs_clsid;/* which slab class we're in */
  uint8_t         nkey;       /* key length, w/terminating null and padding */
  uint32_t        remaining;  /* Max keys to crawl per slab per invocation */
  uint64_t        reclaimed;  /* items reclaimed during this crawl. */
  uint64_t        unfetched;  /* items reclaimed unfetched during this crawl. */
  uint64_t        checked;    /* items examined during this crawl. */
};

/* Header when an item is actually a chunk of another item. */
struct item_chunk {
  pptr<item_chunk> next;     /* points within its own chain. */
  pptr<item_chunk> prev;     /* can potentially point to the head. */
  pptr<item> head;     /* always points to the owner chunk */
  int              size;      /* available chunk space in bytes */
  int              used;      /* chunk space used */
  int              nbytes;    /* used. */
  unsigned short   refcount;  /* used? */
  uint8_t          orig_clsid; /* For obj hdr chunks slabs_clsid is fake. */
  uint8_t          it_flags;  /* ITEM_* above. */
  uint8_t          slabs_clsid; /* Same as above. */
  char data[];
};

#define ITEM_schunk(item) ((char*) &((item)->data) + (item)->nkey + 1 \
    + (item)->nsuffix \
    + (((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

/* current time of day (updated periodically) */
extern volatile rel_time_t *current_time;

/* TODO: Move to slabs.h? */
extern volatile int slab_rebalance_signal;

struct slab_rebalance {
  char *slab_start;
  char *slab_end;
  char *slab_pos;
  int s_clsid;
  int d_clsid;
  uint32_t busy_items;
  uint32_t rescues;
  uint32_t evictions_nomem;
  uint32_t inline_reclaim;
  uint32_t chunk_rescues;
  uint32_t busy_deletes;
  uint32_t busy_loops;
  uint8_t done;
};

extern struct slab_rebalance slab_rebal;
/*
 * Functions
 */
void do_accept_new_conns(const bool do_accept);
struct st_st {
  enum store_item_type sit;
  size_t cas;
};
struct st_st *mk_st (enum store_item_type my_sit, size_t my_cas);

enum delta_result_type do_add_delta(const char *key,
    const size_t nkey, const bool incr,
    const uint64_t delta, uint64_t *value, const uint32_t hv);

std::pair<store_item_type, size_t> do_store_item(item *item, int comm, const uint32_t hv);
extern int daemonize(int nochdir, int noclose);

#define mutex_lock(x) pthread_mutex_lock(x)
#define mutex_unlock(x) pthread_mutex_unlock(x)

#include "assoc.h"
#include "items.h"
#include "crawler.h"

/*
 * Functions such as the libevent-related calls that need to do cross-thread
 * communication in multithreaded mode (rather than actually doing the work
 * in the current thread) are called via "dispatch_" frontends, which are
 * also #define-d to directly call the underlying code in singlethreaded mode.
 */
void memcached_thread_init();
void agnostic_init();

/* Lock wrappers for cache functions that are called from main loop. */
enum delta_result_type add_delta(const char *key,
    const size_t nkey, bool incr,
    const uint64_t delta, uint64_t *value);

item *item_alloc(const char * key, size_t nkey, int flags, rel_time_t exptime, int nbytes);
#define DO_UPDATE true
#define DONT_UPDATE false
item *item_get(const char *key, const size_t nkey, const bool do_update);
item *item_get_locked(const char *key, const size_t nkey, const bool do_update, uint32_t *hv);
item *item_touch(const char *key, const size_t nkey, uint32_t exptime);
int   item_link(item *it);
void  item_remove(item *it);
int   item_replace(item *it, item *new_it, const uint32_t hv);
void  item_unlink(item *it);
enum store_item_type store_item(item *item, int comm);
void* server_thread(void* pargs);

void item_lock(uint32_t hv);
void *item_trylock(uint32_t hv);
void item_trylock_unlock(void *arg);
void item_unlock(uint32_t hv);
void pause_threads(enum pause_thread_types type);
#define refcount_incr(it) ++(it->refcount)
#define refcount_decr(it) --(it->refcount)
void STATS_LOCK(void);
void STATS_UNLOCK(void);
void threadlocal_stats_reset(void);
void threadlocal_stats_aggregate(struct thread_stats *stats);
void slab_stats_aggregate(struct thread_stats *stats, struct slab_stats *out);

/* If supported, give compiler hints for branch prediction. */
#if !defined(__GNUC__) || (__GNUC__ == 2 && __GNUC_MINOR__ < 96)
#define __builtin_expect(x, expected_value) (x)
#endif

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#endif

#pragma once
#include <uuid/uuid.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <list>
#include <map>
#include <type_traits>

#define TOTAL_ALLOC_BUCKETS     14

class FreeList;
class GlobalAlloc;
class ObjectAlloc;

typedef struct {
    void *ptr;
    size_t size;
} memory_region_t;

typedef struct {
    // 64-byte aligned offset of previous chunk
    uint16_t prev_offset;
    uint16_t free_list_id;
    unsigned int last:1; // last chunk in block
    unsigned int used:1; // allocated?
    // future support for chunks larger than block
    unsigned int jumbo:1;
    unsigned int size:29; // chunk size in bytes
} chunk_header_t;

typedef struct free_header_t {
    // <chunk_header_t>
    uint16_t prev_offset;
    uint16_t free_list_id;
    unsigned int last:1;
    unsigned int used:1;
    unsigned int jumbo:1;
    unsigned int size:29;
    // </chunk_header_t>
    struct free_header_t *prev;
    struct free_header_t *next;
} free_header_t;

/*
 * Manages the global heap
 * * Supplies ObjectAlloc with more memory.
 * * Responsible for fixed mapping between restarts.
 * * Maintains the list of allocated pages for snapshots.
 */
class GlobalAlloc {
public:
    GlobalAlloc(const char *snapshot = NULL, const char *bitmap = NULL);
    ~GlobalAlloc();
    static GlobalAlloc *getInstance() {
        if (instance == NULL) instance = new GlobalAlloc();
        return instance;
    }

    void *alloc(const size_t size);
    void release(void *ptr, size_t size);

    void setBitmap(uintptr_t, size_t);
    void unsetBitmap(uintptr_t, size_t);
    void setBitmapForBigAlloc(uintptr_t, size_t);
    void unsetBitmapForBigDealloc(uintptr_t, size_t);

    static size_t snapshotSize();
    void save(char *) const;
    void load(const char *);

    size_t bitmapSize() const;
    void saveBitmap(char *) const;
    void loadBitmap(const char *);

    size_t allocatedBlocks();
    void report();

    ObjectAlloc *newAllocator(uuid_t);
    ObjectAlloc *findAllocator(uuid_t);
    void restoreAllocator(ObjectAlloc *);

protected:
    bool newBlock(memory_region_t *, uintptr_t, size_t);
    void tryMergingRegions(free_header_t *);

private:
    static GlobalAlloc *instance;

    uint64_t *alloc_bitmap = NULL;
    pthread_mutex_t free_list_mutex;
    pthread_mutex_t allocators_mutex;

    free_header_t *free_list = NULL;
    std::list<memory_region_t> mapped_regions;
    std::map<std::string, ObjectAlloc *> allocators;
    ObjectAlloc *allocatorsMemory = NULL;

public:
    static const size_t MaxAllocatorMemorySize = (size_t)2 << 20; // 2 MB
    static const size_t MaxMemorySize = (size_t)1 << 40; // 1 TB
    static const size_t BitmapGranularity = (size_t)1 << 12; // 4 KB
    static const uintptr_t BaseAddress = 0x10000000000;
    static const size_t MinPoolSize = (size_t)256 << 20; // 256 MB
    static_assert(((size_t)1 << 12) == BitmapGranularity,
            "Fix bitmap set and unset methods!");
    static_assert(sizeof(long long) == sizeof(uint64_t),
            "Fix save and load bitmap methods!");
};

/*
 * Allocates memory for a persistent object from volatile pool.
 * During recovery, it restores data from the latest snapshot.
 * * Allocates from local pool or contacts GlobalAlloc for more memory.
 * * Allocations are cache-line (64-byte) aligned.
 * * There are N free lists, where N is the number of physical cores.
 * * Free lists are divided into buckets.
 * * Each free region is added to buckets based on its size.
 * * Pages are restored to the same virtual address after recovery.
 * * We assume a single-socket processor (no NUMA and thus no affinity).
 */
class ObjectAlloc {
public:
    ObjectAlloc(const uuid_t uuid, const char *snapshot = NULL);
    ~ObjectAlloc();
    void *alloc(const size_t size);
    void dealloc(void *ptr);
    void *realloc(void *ptr, const size_t size);

    void save(char *) const;
    void load(const char *);
    size_t snapshotSize() const;

    void print(bool detailed = false) const;

    void releaseFreeBlocks();

private:
    uuid_t my_id;
    size_t total_cores;
    FreeList **free_lists = NULL;

    inline off_t getOffset(const pthread_t thread_id) const;

public:
    static const size_t Alignment = 0x40; // Cache-line width
    static_assert(sizeof(uuid_t) == 2 * sizeof(uint64_t),
            "Fix Object Allocator snapshot methods!");
    friend class GlobalAlloc;
};

/*
 * Manages free lists and allocation buckets for ObjectAlloc.
 * There is one free-list for each physical core.
 * Maximum allocation size is BlockSize, and larger requests fail.
 * Requests GlobalAlloc for new memory at BlockSize granularity.
 * During snapshot, it serves allocation and free requests as below:
 * * Allocations are served using non-allocated pages.
 * * Free requests are postponed till after snapshot completion.
 * * Changes to the bitmap are postponed till after snapshot completion.
 */
class FreeList {
public:
    FreeList(uint16_t);
    ~FreeList();

    void lock();
    void unlock();
    chunk_header_t *alloc(size_t size);
    chunk_header_t *realloc(chunk_header_t *, size_t);
    void dealloc(chunk_header_t *chunk);

    // Allocations of regions larger than the block size (2 MB)
    chunk_header_t *big_alloc(size_t size);
    void big_dealloc(chunk_header_t *chunk);

    void save(char *) const;
    void load(const char *);
    static size_t snapshotSize();

    void print(bool detailed = false) const;

    void releaseFreeBlocks();

private:
    uintptr_t buckets[TOTAL_ALLOC_BUCKETS]; // 64, 128, ...
    uint64_t total_allocated; // bytes
    uint64_t global_allocs; // number of requests for global alloc
    uint64_t chain_lookups; // number of free-list lookups
    uint64_t xlock;
    uint16_t my_id;
#ifndef __OPTIMIZE__
    uint64_t lock_holders;
#endif

    inline off_t findBucket(size_t size) const;
    void addFreeChunk(void *ptr, size_t size);
    chunk_header_t *findBestMatch(size_t size);
    free_header_t *freeAndCoalesce(chunk_header_t *);

    inline uint16_t chunkOffset(void *) const;
    inline bool isLastChunkOfBlock(void *, size_t) const;
    inline chunk_header_t *prevChunk(chunk_header_t *) const;
    inline chunk_header_t *nextChunk(chunk_header_t *) const;
    inline void removeChunkFromBuckets(free_header_t *);

    void printChunk(chunk_header_t *) const;
    void printState(const char *) const;

    static_assert(TOTAL_ALLOC_BUCKETS == 14, "Fix bucket caps!");
    static const uint64_t BucketCaps[14];

public:
    static const size_t BlockSize = (size_t)1 << 21; // 2 MB
    static_assert(BlockSize - 1 == 0x1FFFFF, "Fix free-list block chain!");
    static_assert(sizeof(long long) == sizeof(uintptr_t), "Fix save() and load()!");
    static_assert(sizeof(long long) == sizeof(uintptr_t), "Fix save() and load()!");
};

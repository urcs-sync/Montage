#include <assert.h>
#include <sys/mman.h>
#include "savitar.hpp"
#include "ckpt_alloc.hpp"
#include <emmintrin.h>
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)

/*
 * * * * * * * * * *
 * Global Allocator
 * * * * * * * * * *
 */
GlobalAlloc* GlobalAlloc::instance = NULL;

GlobalAlloc::GlobalAlloc(const char *snapshot, const char *bitmap) {

    assert(instance == NULL);
    assert(MinPoolSize % FreeList::BlockSize == 0);

    // Reserve allocators memory
    // TODO handle object deletes (holes in allocators memory)
    const uintptr_t baseAddress = BaseAddress + MaxMemorySize;
    allocatorsMemory = (ObjectAlloc *)mmap((void *)baseAddress,
            MaxAllocatorMemorySize, PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
    assert(allocatorsMemory != NULL);

    // Allocation map
    alloc_bitmap = (uint64_t *)malloc(bitmapSize());
    if (bitmap != NULL) memcpy(alloc_bitmap, bitmap, bitmapSize());
    else memset(alloc_bitmap, 0, bitmapSize());

    assert(pthread_mutex_init(&allocators_mutex, NULL) == 0);
    assert(pthread_mutex_init(&free_list_mutex, NULL) == 0);
    GlobalAlloc::instance = this;

    if (snapshot != NULL) {
        load(snapshot);
        return;
    }

    // Pre-allocate pool
    memory_region_t region;
    assert(newBlock(&region, BaseAddress, MinPoolSize));
    asm volatile("" ::: "memory");
    mapped_regions.push_back(region);

    // Add pool to free-list
    free_list = (free_header_t *)region.ptr;
    free_list->prev = free_list->next = NULL;
    free_list->size = region.size;
}

GlobalAlloc::~GlobalAlloc() {
    free(alloc_bitmap);
    std::list<memory_region_t>::iterator it;
    for (it = mapped_regions.begin(); it != mapped_regions.end(); ++it) {
        munmap(it->ptr, it->size);
    }

    instance = NULL;
    pthread_mutex_destroy(&free_list_mutex);
    pthread_mutex_destroy(&allocators_mutex);

    mapped_regions.clear();
    allocators.clear();
    munmap(allocatorsMemory, MaxAllocatorMemorySize);
}

// Size of bitmap in bytes
size_t GlobalAlloc::bitmapSize() const {
    return (MaxMemorySize / BitmapGranularity) >> 3;
}

bool GlobalAlloc::newBlock(memory_region_t *region, uintptr_t addr, size_t size) {
#ifdef DEBUG
    fprintf(stdout, "Requesting %zu bytes at %p from the kernel\n", size, (void*)addr);
#endif
    region->ptr = mmap((void *)addr, size, PROT_READ | PROT_WRITE, MAP_SHARED |
            MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
    if (region->ptr == NULL) return false;
    if (region->ptr != (void *)addr) return false;
    region->size = size;
    if (madvise(region->ptr, region->size, MADV_SEQUENTIAL | MADV_WILLNEED) != 0)
        return false;
    return true;
}

void *GlobalAlloc::alloc(const size_t size) {
    void *ptr = NULL;
    free_header_t *head = free_list;

    // TODO find a way to replace locks
    pthread_mutex_lock(&free_list_mutex);

    while (ptr == NULL && head != NULL) { // allocate from pool
        if (head->size < size) {
            head = head->next;
            continue;
        }

        head->size -= size;
        ptr = (char *)head + head->size;
        if (head->size == 0) {
            if (head->next != NULL) head->next->prev = head->prev;
            if (head->prev != NULL) head->prev->next = head->next;
            else free_list = head->next;
        }
    }

    if (ptr == NULL) { // map a new region and allocate from there
        memory_region_t last_region = mapped_regions.back();
        uintptr_t new_region_va = (uintptr_t)last_region.ptr + last_region.size;
        memory_region_t new_region;
        size_t allocSize = MinPoolSize;
        while (allocSize < size) { allocSize *= 2; }
        assert(newBlock(&new_region, new_region_va, allocSize));
        asm volatile("" ::: "memory");
        mapped_regions.push_back(new_region);

        // Serve allocation from the new region
        ptr = (char *)new_region.ptr + new_region.size - size;

        if (new_region.size - size > 0) {
            free_header_t *t = (free_header_t *)new_region.ptr;
            t->prev = NULL;
            t->next = free_list;
            t->size = new_region.size - size;
            if (free_list != NULL) free_list->prev = t;
            free_list = t;
        }
    }

    pthread_mutex_unlock(&free_list_mutex);
    return ptr;
}

void GlobalAlloc::tryMergingRegions(free_header_t *r) {
    bool couldMerge;
    do {
        couldMerge = false;
        free_header_t *t = free_list;
        while (t != NULL) {
            free_header_t *c = t;
            t = t->next;
            if (c == r) continue;

            // Check if `c` and `r` are neighbours
            uintptr_t cPtr = (uintptr_t)c;
            uintptr_t rPtr = (uintptr_t)r;
            if (cPtr != rPtr + r->size && rPtr != cPtr + c->size) continue;

#ifdef DEBUG
            fprintf(stdout, "Merging %p (%d bytes) and %p (%d bytes)\n",
                    c, c->size, r, r->size);
#endif

            // we extend `c` to include `r`
            if (rPtr < cPtr) {
                // swap `c` and `r`
                c = (free_header_t*)((uintptr_t)c ^ (uintptr_t)r);
                r = (free_header_t*)((uintptr_t)c ^ (uintptr_t)r);
                c = (free_header_t*)((uintptr_t)c ^ (uintptr_t)r);
            }
            c->size += r->size;

            // delete `r` from the free list
            if (r->prev != NULL) r->prev->next = r->next;
            else free_list = r->next;
            if (r->next != NULL) r->next->prev = r->prev;

            r = c;
            couldMerge = true;
            break;
        }
    } while (couldMerge);
}

void GlobalAlloc::release(void *ptr, size_t size) {
    pthread_mutex_lock(&free_list_mutex);
    free_header_t *t = (free_header_t *)ptr;
    t->prev = NULL;
    t->next = free_list;
    t->size = size;
    if (free_list != NULL) free_list->prev = t;
    free_list = t;
    tryMergingRegions(t);
    pthread_mutex_unlock(&free_list_mutex);
}

void GlobalAlloc::setBitmap(uintptr_t ptr, size_t size) {

    // TODO optimize for large regions
    uintptr_t limit = ptr + size;
    uintptr_t aligned_ptr = ptr & ~(BitmapGranularity - 1);
    uint64_t bit, page_offset, cell_index;

    while (aligned_ptr < limit) {
        page_offset = (aligned_ptr - BaseAddress) >> 12;
        cell_index = page_offset & 0x3F;
        bit = 1;
        if (cell_index > 0) bit <<= cell_index;
        alloc_bitmap[page_offset >> 6] |= bit;
        aligned_ptr += BitmapGranularity;
    }
}

void GlobalAlloc::unsetBitmap(uintptr_t ptr, size_t size) {
    uintptr_t limit = ptr + size;
    uintptr_t aligned_ptr = ptr & ~(BitmapGranularity - 1);
    uint64_t bit, page_offset, cell_index;

    while (aligned_ptr < limit) {
        page_offset = (aligned_ptr - BaseAddress) >> 12;
        cell_index = page_offset & 0x3F;
        bit = 1;
        if (cell_index > 0) bit <<= cell_index;
        alloc_bitmap[page_offset >> 6] &= ~bit;
        aligned_ptr += BitmapGranularity;
    }
}

void GlobalAlloc::setBitmapForBigAlloc(uintptr_t ptr, size_t size) {
    uintptr_t alignedPtr = ptr & ~(FreeList::BlockSize - 1);
    assert(alignedPtr == ptr);
    const uintptr_t limit = ptr + size;
    uint64_t offset = (ptr - BaseAddress) >> 18; // 12 + 6
    const size_t stepSize = BitmapGranularity << 6; // 64-bit cells

    while (ptr + stepSize <= limit) {
        alloc_bitmap[offset++] = UINT64_MAX;
        ptr += stepSize;
    }

    if (ptr < limit) setBitmap(ptr, limit - ptr);
}

void GlobalAlloc::unsetBitmapForBigDealloc(uintptr_t ptr, size_t blocks) {
    uintptr_t alignedPtr = ptr & ~(FreeList::BlockSize - 1);
    assert(alignedPtr == ptr);

    uint64_t offset = (ptr - BaseAddress) >> 18; // 12 + 6
    const size_t bitmapCells = blocks << 3; // 8 x 64 x 4 KB = 2 MB
    for (size_t i = 0; i < bitmapCells; i++) {
        alloc_bitmap[offset + i] = 0;
    }
}

void GlobalAlloc::saveBitmap(char *nvm) const {
    __m128i *dst = (__m128i *)nvm;
    __m128i *src = (__m128i *)alloc_bitmap;
    __m128i *limit = src + bitmapSize() / sizeof(__m128i);

    while (src < limit) {
        __m128i xmm0 = _mm_loadu_si128(src + 0);
        __m128i xmm1 = _mm_loadu_si128(src + 1);
        __m128i xmm2 = _mm_loadu_si128(src + 2);
        __m128i xmm3 = _mm_loadu_si128(src + 3);
        __m128i xmm4 = _mm_loadu_si128(src + 4);
        __m128i xmm5 = _mm_loadu_si128(src + 5);
        __m128i xmm6 = _mm_loadu_si128(src + 6);
        __m128i xmm7 = _mm_loadu_si128(src + 7);
        __m128i xmm8 = _mm_loadu_si128(src + 8);
        __m128i xmm9 = _mm_loadu_si128(src + 9);
        __m128i xmm10 = _mm_loadu_si128(src + 10);
        __m128i xmm11 = _mm_loadu_si128(src + 11);
        __m128i xmm12 = _mm_loadu_si128(src + 12);
        __m128i xmm13 = _mm_loadu_si128(src + 13);
        __m128i xmm14 = _mm_loadu_si128(src + 14);
        __m128i xmm15 = _mm_loadu_si128(src + 15);

        _mm_stream_si128(dst + 0, xmm0); // 16 bytes
        _mm_stream_si128(dst + 1, xmm1);
        _mm_stream_si128(dst + 2, xmm2);
        _mm_stream_si128(dst + 3, xmm3);
        _mm_stream_si128(dst + 4, xmm4);
        _mm_stream_si128(dst + 5, xmm5);
        _mm_stream_si128(dst + 6, xmm6);
        _mm_stream_si128(dst + 7, xmm7);
        _mm_stream_si128(dst + 8, xmm8);
        _mm_stream_si128(dst + 9, xmm9);
        _mm_stream_si128(dst + 10, xmm10);
        _mm_stream_si128(dst + 11, xmm11);
        _mm_stream_si128(dst + 12, xmm12);
        _mm_stream_si128(dst + 13, xmm13);
        _mm_stream_si128(dst + 14, xmm14);
        _mm_stream_si128(dst + 15, xmm15);

        src += 16;
        dst += 16;
    }
    //_mm_sfence();
}

void GlobalAlloc::loadBitmap(const char *nvm) {
    memcpy(alloc_bitmap, nvm, bitmapSize());
}

size_t GlobalAlloc::snapshotSize() {
    size_t sz = 0;
    sz += sizeof(uint64_t); // Total allocated
    sz += sizeof(uint64_t); // Size of free-list
    // Max number of free pages (address and size)
    sz += ((MinPoolSize >> 21) << 1);
    return sz;
}

void GlobalAlloc::save(char *nvm) const {
    long long *ptr = (long long *)nvm;
    long long *total_allocated = ptr++;
    long long *free_list_length = ptr++;

    *total_allocated = 0;
    std::list<memory_region_t>::const_iterator it;
    for (it = mapped_regions.begin(); it != mapped_regions.end(); ++it) {
        *total_allocated += it->size;
    }

    *free_list_length = 0;
    free_header_t *f = free_list;
    while (f != NULL) {
        _mm_stream_si64(ptr++, (long long)f); // address
        _mm_stream_si64(ptr++, f->size); // size
        *free_list_length = *free_list_length + 1;
        f = f->next;
    }

    _mm_clflush(total_allocated);
    //_mm_sfence();
}

void GlobalAlloc::load(const char *nvm) {
    uint64_t *ptr = (uint64_t *)nvm;
    size_t pool_size = ptr[0];
    size_t free_list_length = ptr[1];
    ptr = ptr + 2;

    // reconstruct mapped_regions
    memory_region_t region;
    assert(newBlock(&region, BaseAddress, pool_size));
    asm volatile("" ::: "memory");
    mapped_regions.push_back(region);

    // reconstruct free_list
    free_list = NULL;
    free_header_t *prev = NULL;
    for (size_t i = 0; i < free_list_length; i++) {
        free_header_t *f = (free_header_t *)ptr[0];
        f->size = ptr[1];

        if (prev == NULL) free_list = f;
        else prev->next = f;
        f->prev = prev;
        f->next = NULL;

        prev = f;
        ptr = ptr + 2;
    }

#if DEBUG
    fprintf(stdout, "------------------------------------------\n");
    fprintf(stdout, "Loaded the Global Allocator from snapshot\n");
    fprintf(stdout, "Pool size:\t\t%zu\n", pool_size);
    fprintf(stdout, "Free list length:\t%zu\n", free_list_length);
    free_header_t *t = free_list;
    while (t != NULL) {
        fprintf(stdout, "> %p\t\t%d MB\n", t, (t->size >> 20));
        if (t->next != NULL) assert(t->next->prev == t);
        t = t->next;
    }
    fprintf(stdout, "------------------------------------------\n");
#endif
}

size_t GlobalAlloc::allocatedBlocks() {
    size_t blocks = 0;
    pthread_mutex_lock(&free_list_mutex);
    memory_region_t last_region = mapped_regions.back();
    blocks = ((uintptr_t)last_region.ptr - BaseAddress +
        last_region.size) / FreeList::BlockSize;
    pthread_mutex_unlock(&free_list_mutex);
    return blocks;
}

void GlobalAlloc::report() {

    pthread_mutex_lock(&free_list_mutex);

    memory_region_t last_region = mapped_regions.back();
    size_t allocatedBytes = ((uintptr_t)last_region.ptr - BaseAddress) +
        last_region.size;
    size_t allocatedBlocks = allocatedBytes / FreeList::BlockSize;

    size_t modifiedBits = 0, unmodifiedBits = 0;
    for (unsigned int b = 0; b < allocatedBlocks; b++) {
        for (unsigned int c = 0; c < 8; c++) {
            uint64_t bit = alloc_bitmap[b * 8 + c];
            for (unsigned int p = 0; p < 64; p++) {
                if (bit & 0x0000000000000001) modifiedBits++;
                else unmodifiedBits++;
                bit = bit >> 1;
            }
        }
    }

    pthread_mutex_unlock(&free_list_mutex);

    fprintf(stdout, "Global Allocator Report\n");
    fprintf(stdout, "- Allocated blocks: %zu\n", allocatedBlocks);
    fprintf(stdout, "- Allocated bytes: %zu\n", allocatedBytes);
    fprintf(stdout, "- Modified pages: %zu\n", modifiedBits);
    fprintf(stdout, "- Unmodified pages: %zu\n", unmodifiedBits);
}

ObjectAlloc *GlobalAlloc::newAllocator(uuid_t uuid) {
    char uuid_str[64];
    uuid_unparse(uuid, uuid_str);

    pthread_mutex_lock(&allocators_mutex);
    // TODO make sure we do not exceed the upper limit of mapped region
    void *ptr = (void *)&allocatorsMemory[allocators.size()];
    ObjectAlloc *alloc = new (ptr) ObjectAlloc(uuid);
    allocators.insert(pair<std::string, ObjectAlloc *>(uuid_str, alloc));
    pthread_mutex_unlock(&allocators_mutex);

    return alloc;
}

ObjectAlloc *GlobalAlloc::findAllocator(uuid_t uuid) {
    char uuid_str[64];
    uuid_unparse(uuid, uuid_str);

    pthread_mutex_lock(&allocators_mutex);
    auto it = allocators.find(uuid_str);
    pthread_mutex_unlock(&allocators_mutex);

    if (it == allocators.end()) return NULL;
    return it->second;
}

void GlobalAlloc::restoreAllocator(ObjectAlloc *alloc) {
    char uuid_str[64];
    uuid_unparse(alloc->my_id, uuid_str);

    pthread_mutex_lock(&allocators_mutex);
    allocators.insert(pair<std::string, ObjectAlloc *>(uuid_str, alloc));
    pthread_mutex_unlock(&allocators_mutex);
}

/*
 * * * * * * * * * *
 * Object Allocator
 * * * * * * * * * *
 */
void get_cpu_info(uint8_t *core_map, int *map_size);

ObjectAlloc::ObjectAlloc(const uuid_t uuid, const char *snapshot) {

    int cores;
    uint8_t core_info[MAX_CORES];
    get_cpu_info(core_info, &cores);
    total_cores = cores;

    // Create free lists
    free_lists = (FreeList **)malloc(sizeof(FreeList *) * cores);
    for (uint16_t c = 0; c < total_cores; c++) {
        free_lists[c] = new FreeList(c);
    }
    uuid_copy(my_id, uuid);

    // Recover if snapshot is provided
    if (snapshot != NULL) load(snapshot);
}

ObjectAlloc::~ObjectAlloc() {
    // TODO support permanent delete
    for (size_t c = 0; c < total_cores; c++) {
        delete free_lists[c];
    }
    free(free_lists);
}

off_t ObjectAlloc::getOffset(const pthread_t thread_id) const {
    uint64_t key = (uint64_t)thread_id;
    key = (~key) + (key << 21);
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return (off_t)(key % total_cores);
}

void *ObjectAlloc::alloc(const size_t size) {

    size_t chunk_size = size + sizeof(chunk_header_t);
    if (chunk_size % Alignment != 0) {
        chunk_size += Alignment - (chunk_size % Alignment);
    }

    void *ptr = NULL;
    FreeList *free_list = free_lists[getOffset(pthread_self())];
    free_list->lock();

    if (chunk_size <= FreeList::BlockSize) {
        chunk_header_t *chunk = free_list->alloc(chunk_size);
        if (chunk != NULL) {
            ptr = (char *)chunk + sizeof(chunk_header_t);
            GlobalAlloc::getInstance()->setBitmap(
                    (uintptr_t)chunk, chunk_size);
        }
    }
    else {
        chunk_header_t *chunk = free_list->big_alloc(chunk_size);
        if (chunk != NULL) {
            GlobalAlloc::getInstance()->setBitmapForBigAlloc(
                    (uintptr_t)chunk, chunk_size);
            ptr = (char *)chunk + sizeof(chunk_header_t);
        }
    }

    free_list->unlock();
    return ptr;
}

/*
 * Returns freed region to its parent free-list (not necessarily thread-local)
 * For big allocations, freed regions are returned to the Global Allocator
 */
void ObjectAlloc::dealloc(void *ptr) {
    if (ptr == NULL) return;
    chunk_header_t *chunk = (chunk_header_t *)((char *)ptr -
            sizeof(chunk_header_t));
    FreeList *free_list = free_lists[chunk->free_list_id];
    free_list->lock();
    if (chunk->size <= FreeList::BlockSize) free_list->dealloc(chunk);
    else free_list->big_dealloc(chunk);
    free_list->unlock();
}

void *ObjectAlloc::realloc(void *ptr, const size_t size) {
    if (ptr == NULL) return NULL;

    const size_t block_size = FreeList::BlockSize;
    size_t new_size = size + sizeof(chunk_header_t);
    if (new_size % Alignment != 0)
        new_size += Alignment - (new_size % Alignment);
    // TODO add support for regions larger than block size
    if (new_size > block_size) return NULL;

    chunk_header_t *chunk = (chunk_header_t *)((char *)ptr -
            sizeof(chunk_header_t));
    FreeList *free_list = free_lists[chunk->free_list_id];
    free_list->lock();
    chunk = free_list->realloc(chunk, new_size);
    free_list->unlock();
    // Note: bitmap is already updated by the free-list
    return (void *)((char *)chunk + sizeof(chunk_header_t));
}

void ObjectAlloc::print(bool detailed) const {
    for (size_t c = 0; c < total_cores; c++) {
        free_lists[c]->print(detailed);
    }
}

void ObjectAlloc::save(char *nvm) const {
    long long *ptr = (long long *)nvm;

    _mm_stream_si64(&ptr[0], ((long long *)my_id)[0]);
    _mm_stream_si64(&ptr[1], ((long long *)my_id)[1]);
    _mm_stream_si64(&ptr[2], total_cores);
    _mm_stream_si64(&ptr[3], (long long)this);
    ptr = &ptr[4];

    for (size_t c = 0; c < total_cores; c++) {
        free_lists[c]->save((char *)ptr);
        ptr += FreeList::snapshotSize() / sizeof(*ptr);
    }

    //_mm_sfence();
}

void ObjectAlloc::load(const char *nvm) {
    uuid_t uuid;
    memcpy(uuid, nvm, sizeof(uuid_t));
    assert(uuid_compare(my_id, uuid) == 0);

    // Only supports same number of cores for now
    long long *ptr = (long long *)nvm + 2;
    assert(*ptr == total_cores);
    ptr += 2; // Skip object pointer

#ifdef DEBUG
    fprintf(stdout, "------------------------------------------\n");
    fprintf(stdout, "Recovered Object Allocator from snapshot\n");
    char uuid_str[64];
    uuid_unparse(uuid, uuid_str);
    uuid_str[9] = uuid_str[10] = uuid_str[11] = '.';
    uuid_str[12] = '\0';
    fprintf(stdout, "Identifier:\t\t%s\n", uuid_str);
    fprintf(stdout, "Total cores:\t\t%zu\n", total_cores);
    fprintf(stdout, "Non-empty free lists:\n");
#endif

    for (size_t c = 0; c < total_cores; c++) {
        free_lists[c]->load((const char *)ptr);
        ptr += FreeList::snapshotSize() / sizeof(*ptr);
    }

#ifdef DEBUG
    fprintf(stdout, "------------------------------------------\n");
#endif
}

size_t ObjectAlloc::snapshotSize() const {
    size_t sz = 0;
    sz += sizeof(uuid_t);
    sz += sizeof(uint64_t); // total cores
    sz += sizeof(uintptr_t); // this
    sz += total_cores * FreeList::snapshotSize();
    return sz;
}

void ObjectAlloc::releaseFreeBlocks() {
    for (size_t c = 0; c < total_cores; c++) {
        free_lists[c]->releaseFreeBlocks();
    }
}

/*
 * * * * * *
 * Free List
 * * * * * *
 */
const uint64_t FreeList::BucketCaps[14] = {
    0x00040, 0x00080, // 64 bytes and 128 bytes
    0x00100, 0x00200, // 256 bytes and 512 bytes
    0x00400, 0x00800, // 1 KB and 2 KB
    0x01000, 0x02000, // 4 KB and 8 KB
    0x04000, 0x08000, // 16 KB and 32 KB
    0x10000, 0x20000, // 64 KB and 128 KB
    0x40000, UINT64_MAX // 256 KB and above
};

FreeList::FreeList(uint16_t id) {
    my_id = id;
    xlock = 0;
#ifndef __OPTIMIZE__
    lock_holders = 0;
#endif

    total_allocated = 0;
    chain_lookups = 0;
    global_allocs = 0;

    for (int i = 0; i < TOTAL_ALLOC_BUCKETS; i++) {
        buckets[i] = (uintptr_t)NULL;
    }
    asm volatile("" ::: "memory");
}

FreeList::~FreeList() {
    // TODO support for permanent delete
}

void FreeList::lock() {
    while (!__sync_bool_compare_and_swap(&xlock, 0, 1));
#ifndef __OPTIMIZE__
    assert(__sync_add_and_fetch(&lock_holders, 1) == 1);
#endif
}

void FreeList::unlock() {
#ifdef __OPTIMIZE__
    __sync_bool_compare_and_swap(&xlock, 1, 0);
#else
    assert(__sync_sub_and_fetch(&lock_holders, 1) == 0);
    assert(__sync_bool_compare_and_swap(&xlock, 1, 0));
#endif
}

off_t FreeList::findBucket(size_t size) const {
    off_t bucket = 0;
    while (size > BucketCaps[bucket]) bucket++;
    return bucket;
}

uint16_t FreeList::chunkOffset(void *chunk) const {
    return ((uintptr_t)chunk & 0x1FFFFF) >> 6;
}

bool FreeList::isLastChunkOfBlock(void *chunk, size_t size) const {
    return ((uintptr_t)chunk & 0x1FFFFF) + size == 0x200000;
}

chunk_header_t *FreeList::prevChunk(chunk_header_t *chunk) const {
    uint16_t offset = chunkOffset(chunk);
    if (offset == 0) return NULL;
    char *ptr = (char *)chunk - ((offset - chunk->prev_offset) << 6);
    return (chunk_header_t *)ptr;
}

chunk_header_t *FreeList::nextChunk(chunk_header_t *chunk) const {
    if (chunk->last == 1) return NULL;
    return (chunk_header_t *)((char *)chunk + chunk->size);
}

chunk_header_t *FreeList::findBestMatch(size_t size) {

    // Look for a chunk with proper size
    off_t bucket = findBucket(size);
#ifndef __OPTIMIZE__
    assert(bucket < TOTAL_ALLOC_BUCKETS);
#endif
    chunk_header_t *chunk = NULL;

    while (chunk == NULL && bucket < TOTAL_ALLOC_BUCKETS) {
        free_header_t *fc = (free_header_t *)buckets[bucket];
        while (++chain_lookups && fc != NULL) {
            if (size <= fc->size) { // Found a big enough free chunk
                size_t remainder = fc->size - size;
                if (bucket > 0 && remainder > BucketCaps[bucket - 1]) {
                    // No need to remove free chunk
                    chunk = (chunk_header_t *)((char *)fc + remainder);
                    chunk->prev_offset = chunkOffset(fc);
                    chunk->used = 0;

                    // Update next chunk
                    if (fc->last != 1) {
                        nextChunk((chunk_header_t *)fc)->prev_offset =
                            chunkOffset(chunk);
                    }
                    fc->last = 0;
                    fc->size = remainder;
                    // no change for used, prev_offset and free_list_id
                }
                else { // Remove free chunk
                    chunk = (chunk_header_t *)fc;
                    if (fc->prev != NULL) fc->prev->next = fc->next;
                    else buckets[bucket] = (uintptr_t)fc->next;
                    if (fc->next != NULL) fc->next->prev = fc->prev;

                    // Split free chunk if necessary
                    if (remainder > 0) {
                        fc = (free_header_t *)((char *)fc + size);
                        fc->prev_offset = chunkOffset(chunk);
                        fc->free_list_id = my_id;
                        fc->last = isLastChunkOfBlock(fc, fc->size);
                        fc->used = 0;
                        fc->size = remainder;
                        if (fc->last != 1) {
                            ((chunk_header_t *)((char *)fc +
                                fc->size))->prev_offset = chunkOffset(fc);
                        }
                        addFreeChunk(fc, fc->size);
                    }
                }
                break; // Found free chunk (skips to bucket++)
            }
            fc = fc->next; // Does not run if chunk != NULL
        }
        bucket++; // no effect if chunk != NULL
    }

#ifndef __OPTIMIZE__
    assert(chunk == NULL || chunk->used == 0);
#endif
    return chunk;
}

chunk_header_t *FreeList::alloc(size_t size) {
    chunk_header_t *chunk = findBestMatch(size);

    if (chunk == NULL) {
        // Not found, ask GlobalAlloc for more memory
        chunk = (chunk_header_t *)GlobalAlloc::getInstance()->alloc(BlockSize);
#ifndef __OPTIMIZE__
        chunk->used = 0;
        assert(chunk != NULL);
#endif
        chunk->prev_offset = 0;
        if (size < BlockSize) {
            free_header_t *fc = (free_header_t *)((char *)chunk + size);
            fc->prev_offset = 0;
            fc->free_list_id = my_id;
            fc->last = 1;
            fc->used = 0;
            fc->size = BlockSize - size;
            addFreeChunk(fc, fc->size);
        }
        global_allocs++;
    }

    // Update stats
    if (chunk != NULL) {
        chunk->free_list_id = my_id;
        chunk->last = isLastChunkOfBlock(chunk, size);
#ifndef __OPTIMIZE__
        assert(chunk->free_list_id == my_id);
        if (chunk->used != 0) printChunk(chunk);
        assert(chunk->used == 0);
#endif
        chunk->used = 1;
        chunk->size = size;

        total_allocated += size;
    }

    return chunk;
}

void FreeList::removeChunkFromBuckets(free_header_t *chunk) {
        if (chunk->prev != NULL) chunk->prev->next = chunk->next;
        else buckets[findBucket(chunk->size)] = (uintptr_t)chunk->next;
        if (chunk->next != NULL) chunk->next->prev = chunk->prev;
}

free_header_t *FreeList::freeAndCoalesce(chunk_header_t *chunk) {

    free_header_t *prev = (free_header_t *)prevChunk(chunk);
    free_header_t *next = (free_header_t *)nextChunk(chunk);
    chunk->used = 0;

    // No need for coalescing when chunk.size == BlockSize
    // Wentao: commenting this out to disable coalescing since it's buggy.
    // if (prev == NULL && next == NULL) 
        return (free_header_t *)chunk;

    if (prev != NULL && prev->used == 0) { // coalesce with previous
        removeChunkFromBuckets(prev);

        // Extend prev to include chunk
        prev->size += chunk->size;
        prev->last = chunk->last;
        chunk = (chunk_header_t *)prev;

        // Update next chunk if not free
        if (next != NULL && next->used == 1) {
            next->prev_offset = chunkOffset(chunk);
        }
    }

    if (next != NULL && next->used == 0) { // coalesce with next
        removeChunkFromBuckets(next);

        // Expand chunk to include next
        chunk->size += next->size;
        chunk->last = next->last;

        // Update next-next if necessary
        if (chunk->last != 1) {
            next = (free_header_t *)nextChunk(chunk);
            next->prev_offset = chunkOffset(chunk);
        }
    }

    return (free_header_t *)chunk;
}

void FreeList::dealloc(chunk_header_t *chunk) {
#ifndef __OPTIMIZE__
    assert(chunk->used == 1);
#endif
    size_t chunk_size = chunk->size;
    free_header_t *fc = freeAndCoalesce(chunk);
    addFreeChunk(fc, fc->size);
#ifndef __OPTIMIZE__
    assert(total_allocated >= chunk_size);
#endif
    total_allocated -= chunk_size;

    // Unset bitmap if necessary
    if (fc->size < GlobalAlloc::BitmapGranularity) return;

    uintptr_t mask = ~(GlobalAlloc::BitmapGranularity - 1);
    uintptr_t lb = (uintptr_t)chunk & mask;
    uintptr_t ub = ((uintptr_t)chunk + chunk_size) & mask;

    if (lb < (uintptr_t)fc)
        lb += GlobalAlloc::BitmapGranularity;
    if (ub + GlobalAlloc::BitmapGranularity <= (uintptr_t)fc + fc->size)
        ub = ub + GlobalAlloc::BitmapGranularity;
    if (ub - lb > 0)
        GlobalAlloc::getInstance()->unsetBitmap(lb, ub - lb);
}

void FreeList::addFreeChunk(void *ptr, size_t size) {
    off_t bucket = findBucket(size);
    free_header_t *fc = (free_header_t *)ptr;
    fc->prev = NULL;
    fc->next = (free_header_t *)buckets[bucket];
    if (fc->next != NULL) fc->next->prev = fc;
    buckets[bucket] = (uintptr_t)fc;
}

chunk_header_t *FreeList::realloc(chunk_header_t *chunk, size_t new_size) {
    if (new_size <= chunk->size) return chunk;
    size_t increase = new_size - chunk->size;

    chunk_header_t *next = nextChunk(chunk);
    if (next == NULL || next->used == 1 || next->size < increase) {
        chunk_header_t *new_chunk = alloc(new_size);
        char *src = (char *)chunk + sizeof(chunk_header_t);
        char *dst = (char *)new_chunk + sizeof(chunk_header_t);
        memcpy(dst, src, chunk->size - sizeof(chunk_header_t));
        dealloc(chunk); // clears the bitmap
        chunk = new_chunk;
        // Set the bitmap for the larger chunk
        GlobalAlloc::getInstance()->setBitmap((uintptr_t)chunk, new_size);
    }
    else { // chunk can be extended
        free_header_t *fc = (free_header_t *)next;
        removeChunkFromBuckets(fc);
        next = nextChunk((chunk_header_t *)fc);

        if (fc->size > increase) { // split
            memcpy((char *)fc + increase, fc, sizeof(chunk_header_t));
            fc = (free_header_t *)((char *)fc + increase);
            fc->size -= increase;
            if (next != NULL)
                next->prev_offset = chunkOffset(fc);
            addFreeChunk(fc, fc->size);
        }
        else { // use the entire chunk
            if (next != NULL)
                next->prev_offset = fc->prev_offset;
            chunk->last = fc->last;
        }

        GlobalAlloc::getInstance()->setBitmap((uintptr_t)chunk + chunk->size,
                increase);
        chunk->size = new_size;
    }

    return chunk;
}

chunk_header_t *FreeList::big_alloc(size_t size) {

    size_t allocSize = size & ~(BlockSize - 1);
    if (size > allocSize) allocSize += BlockSize;

    void *ptr = GlobalAlloc::getInstance()->alloc(allocSize);
    chunk_header_t *chunk = (chunk_header_t *)ptr;

    chunk->prev_offset = 0;
    chunk->free_list_id = my_id;
    chunk->last = 1;
    chunk->used = 1;
    chunk->jumbo = 1;
    chunk->size = size;

    return chunk;
}

void FreeList::big_dealloc(chunk_header_t *chunk) {
    size_t blocks = chunk->size / BlockSize;
    if (chunk->size % BlockSize != 0) blocks++;
    memset(chunk, 0, sizeof(chunk_header_t));
    GlobalAlloc::getInstance()->unsetBitmapForBigDealloc(
            (uintptr_t)chunk, blocks);
    GlobalAlloc::getInstance()->release(chunk, blocks * BlockSize);
}

void FreeList::print(bool detailed) const {
    if (global_allocs == 0) return;
    fprintf(stdout, "------------------------------------------\n");
    fprintf(stdout, "Free-list ID: %d\n", my_id);
    fprintf(stdout, "Total allocated: %zu bytes\n", total_allocated);
    fprintf(stdout, "Chain lookups: %zu\n", chain_lookups);
    fprintf(stdout, "Global allocations: %zu\n", global_allocs);
    if (detailed) {
        for (int i = 0; i < TOTAL_ALLOC_BUCKETS; i++) {
            fprintf(stdout, "\tBucket[%d]: %p (", i, (void *)buckets[i]);
            uint64_t chunks = 0, bytes = 0;
            free_header_t *chunk = (free_header_t *)buckets[i];
            while (chunk != NULL) {
                bytes += chunk->size;
                chunk = chunk->next;
                chunks++;
            }
            fprintf(stdout, "%zu bytes in %zu chunks)\n", bytes, chunks);
        }
    }
    fprintf(stdout, "------------------------------------------\n");
}

void FreeList::printState(const char *tag) const {
    fprintf(stdout, "|> %s: ", tag);
    fprintf(stdout, "-- ID: %d\tAllocated: %zu\tGlobal: %zu\n",
            my_id, total_allocated, global_allocs);
    for (int i = 0; i < TOTAL_ALLOC_BUCKETS; i++) {
        free_header_t *chunk = (free_header_t *)buckets[i];
        if (chunk == NULL) continue;

        uint64_t chunks = 0, bytes = 0;
        while (chunk != NULL) {
            bytes += chunk->size;
            chunk = chunk->next;
            chunks++;
        }
        fprintf(stdout, "-- B[%d]: %zu bytes | %zu chunks\n", i, bytes, chunks);
    }
}

void FreeList::printChunk(chunk_header_t *c) const {
    fprintf(stdout, "------------------------------------------\n");
    fprintf(stdout, "CHUNK = { ");
    fprintf(stdout, "prev_offset: %d, ", c->prev_offset);
    fprintf(stdout, "free_list_id: %d, ", c->free_list_id);
    fprintf(stdout, "last: %d, used: %d, ", c->last, c->used);
    fprintf(stdout, "size: %d }\n", c->size);

    chunk_header_t *prev = prevChunk(c);
    chunk_header_t *prevPrev = prev != NULL ? prevChunk(prev) : NULL;
    chunk_header_t *next = nextChunk(c);
    chunk_header_t *nextNext = next != NULL ? nextChunk(next) : NULL;
    chunk_header_t *chunks[5] = {prevPrev, prev, c, next, nextNext};

    fprintf(stdout, "LAYOUT: ");
    for (int i = 0; i < 5; i++) {
        chunk_header_t *chunk = chunks[i];
        if (chunk != NULL) {
            char fill = chunk->used ? 'U' : 'F';
            fprintf(stdout, "<%c>%d|%d<%c>",
                    fill, chunk->prev_offset * 64, chunk->size, fill);
        }
        else {
            fprintf(stdout, "|------|");
        }
    }
    fprintf(stdout, "\n");
    fprintf(stdout, "------------------------------------------\n");
}

void FreeList::save(char *nvm) const {
    long long *ptr = (long long *)nvm;
    _mm_stream_si64(&ptr[0], my_id);
    _mm_stream_si64(&ptr[1], total_allocated);
    _mm_stream_si64(&ptr[2], global_allocs);
    _mm_stream_si64(&ptr[3], chain_lookups);
    for (int i = 0; i < TOTAL_ALLOC_BUCKETS; i++) {
        _mm_stream_si64(&ptr[4 + i], buckets[i]);
    }
}

void FreeList::load(const char *nvm) {
    long long *ptr = (long long *)nvm;
    my_id = ptr[0];
    total_allocated = ptr[1];
    global_allocs = ptr[2];
    chain_lookups = ptr[3];
    size_t sz = TOTAL_ALLOC_BUCKETS * sizeof(uintptr_t);
    memcpy(buckets, &ptr[4], sz);

#ifdef DEBUG
    if (global_allocs == 0) return;
    fprintf(stdout, "[%d]\tAllocated:\t%zu bytes\n", my_id, total_allocated);
    fprintf(stdout, "\tLookups:\t%zu\n", chain_lookups);
    fprintf(stdout, "\tGlobal:\t\t%zu\n", global_allocs);
    for (int i = 0; i < TOTAL_ALLOC_BUCKETS; i++) {
        if (buckets[i] == 0) continue;
        fprintf(stdout, "\t[%d]", i);
        free_header_t *t = (free_header_t *)buckets[i];
        while (t != NULL) {
            fprintf(stdout, " [%p : %d]", t, t->size);
            t = t->next;
        }
        fprintf(stdout, "\n");
    }
#endif
}

size_t FreeList::snapshotSize() {
    size_t sz = 0;
    sz += sizeof(uint64_t); // my_id
    sz += sizeof(uint64_t); // total_allocated
    sz += sizeof(uint64_t); // global_allocs
    sz += sizeof(uint64_t); // chain_lookups
    sz += TOTAL_ALLOC_BUCKETS * sizeof(uint64_t);
    return sz;
}

void FreeList::releaseFreeBlocks() {
    free_header_t *chunk = (free_header_t *)buckets[TOTAL_ALLOC_BUCKETS - 1];
    while (chunk != NULL) {
        free_header_t *prev = chunk->prev;
        free_header_t *next = chunk->next;

        if (chunk->size == BlockSize) {
            if (prev != NULL) prev->next = next;
            else buckets[TOTAL_ALLOC_BUCKETS - 1] = (uintptr_t)next;
            if (next != NULL) next->prev = prev;

            GlobalAlloc::getInstance()->release(chunk, BlockSize);
        }

        chunk = next;
    }
}

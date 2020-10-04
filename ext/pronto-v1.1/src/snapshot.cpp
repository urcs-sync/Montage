#include "snapshot.hpp"
#include "ckpt_alloc.hpp"
#include "nvm_manager.hpp"
#include "nv_object.hpp"
#include "thread.hpp"
#include "recovery_context.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <emmintrin.h>
#include <thread>
#include <errno.h>
#include <inttypes.h>
#include <algorithm>

#define CAS(a,b,c) __sync_bool_compare_and_swap(a,b,c)

Snapshot *Snapshot::instance = NULL;

Snapshot::Snapshot(const char *snapshotPath) {
    assert(Snapshot::instance == NULL);
    rootPath = snapshotPath;
    fd = 0;
    view = NULL;
    context = NULL;
    instance = this;
}

Snapshot::~Snapshot() {
    instance = NULL;
}

Snapshot *Snapshot::getInstance() {
    assert(instance != NULL);
    return instance;
}

bool Snapshot::anyActiveSnapshot() {
    return instance != NULL;
}

void Snapshot::getExistingSnapshots(std::vector<uint32_t> &snapshots) {
    for (auto &p : experimental::filesystem::directory_iterator(rootPath)) {
        experimental::filesystem::path filePath = p.path();
        if (filePath.stem() != "snapshot") continue;
        std::string extension = filePath.extension().string().substr(1);
        snapshots.push_back(std::stoi(extension));
    }

    if (snapshots.empty()) return;
    std::sort(snapshots.begin(), snapshots.end());
}

uint32_t Snapshot::lastSnapshotID() {
    std::vector<uint32_t> snapshots;
    getExistingSnapshots(snapshots);
    if (snapshots.empty()) return 0;
    else return snapshots.back();
}

/*
 * Snapshot layout
 * [header][bitmaps][allocations][data]
 */
void Snapshot::prepareSnapshot() {
    // Calculate snapshot size (excluding data)
    GlobalAlloc *instance = GlobalAlloc::getInstance();
    size_t snapshotSize = sizeof(snapshot_header_t);
    snapshotSize += instance->bitmapSize();
    snapshotSize += instance->snapshotSize();

    uint32_t objectCount = 0;
    for (auto it = NVManager::getInstance().objects.begin();
            it != NVManager::getInstance().objects.end(); it++) {
        snapshotSize += sizeof(uint64_t); // last committed log
        snapshotSize += sizeof(uint64_t); // log tail
        snapshotSize += sizeof(uintptr_t); // object pointer
        snapshotSize += it->second->alloc->snapshotSize();
        objectCount++;
    }

    const size_t alignment = 64;
    if (snapshotSize % alignment != 0) {
        snapshotSize += alignment - (snapshotSize % alignment);
    }

    // Create and map snapshot file
    experimental::filesystem::path poolPath = rootPath;
    poolPath /= "snapshot.";
    poolPath += std::to_string(lastSnapshotID() + 1);
    fd = open(poolPath.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
    assert(fd > 0);
    assert(fallocate(fd, 0, 0, snapshotSize) == 0);
    view = (snapshot_header_t *)mmap(NULL, snapshotSize,
            PROT_READ | PROT_WRITE, MAP_SHARED_VALIDATE | MAP_SYNC, fd, 0);
    assert(view != NULL);
    assert(madvise(view, snapshotSize, MADV_SEQUENTIAL | MADV_WILLNEED) == 0);

    // Initialize snapshot header
    view->identifier = lastSnapshotID();
    view->time = 0;
    view->size = snapshotSize;
    view->object_count = objectCount;
    view->bitmap_offset = sizeof(snapshot_header_t);
    view->global_offset = view->bitmap_offset + instance->bitmapSize();
    view->alloc_offset = view->global_offset + instance->snapshotSize();
    view->data_offset = snapshotSize;

    // Initialize snapshot context
    context = (uint64_t *)malloc(instance->bitmapSize() / 8);
    memset(context, 'F', instance->bitmapSize() / 8);
    // 2 MB = 64 x 4 KB -- 8 x sizeof(uint64_t) = 64
}

void Snapshot::waitForFaultHandlers(size_t allocatedBlocks) {
    // Make sure all fault handlers have returned
    for (size_t i = 0; i < allocatedBlocks; i++) {
        while (context[i] == LockedHugePage) usleep(1);
    }
}

uint32_t Snapshot::create() {
    // Block creation of new persistent objects
    NVManager::getInstance().lock();

    struct timespec t1, t2, t3;
    prepareSnapshot();

    // Extend the snapshot off the critical path
    // Leave some slack for Global Allocator (slack = MinPoolSize)
    // TODO need to communicate this with the allocator
    size_t allocatedBlocks = GlobalAlloc::getInstance()->allocatedBlocks();
    allocatedBlocks += GlobalAlloc::MinPoolSize / FreeList::BlockSize;
    extendSnapshot(allocatedBlocks);

    // Freeze the system (begin synchronous snapshot)
    clock_gettime(CLOCK_REALTIME, &t1);
    blockNewTransactions();
    waitForRunningTransactions();

    // Save allocation tables and mark pages as R/O
    allocatedBlocks = GlobalAlloc::getInstance()->allocatedBlocks();
    saveAllocationTables();
    markPagesReadOnly();

    // Start the asynchronous stage (begin asynchronous snapshot)
    clock_gettime(CLOCK_REALTIME, &t2);
    unblockNewTransactions();
    saveModifiedPages(allocatedBlocks);
    waitForFaultHandlers(allocatedBlocks);
    _mm_clflush(view);
    clock_gettime(CLOCK_REALTIME, &t3);

    // Update snapshot header
    view->time = time(NULL);

    // Finalize the snapshot
    uint64_t latency = (t2.tv_sec - t1.tv_sec) * 1E9;
    latency += (t2.tv_nsec - t1.tv_nsec);
    view->sync_latency = latency / 1E3; // us
    latency = (t3.tv_sec - t2.tv_sec) * 1E9;
    latency += (t3.tv_nsec - t2.tv_nsec);
    view->async_latency = latency / 1E3; // us
    cleanEnvironment();
    NVManager::getInstance().unlock();

    return lastSnapshotID() - 1;
}

void Snapshot::pageFaultHandler(void *addr) {

    const uintptr_t LB = GlobalAlloc::BaseAddress;
    const uintptr_t UB = GlobalAlloc::BaseAddress + GlobalAlloc::MaxMemorySize;
    uintptr_t alignedAddr = (uintptr_t)addr & ~(FreeList::BlockSize - 1);
    assert(alignedAddr >= LB && alignedAddr < UB);

    const size_t PPBlk = FreeList::BlockSize / GlobalAlloc::BitmapGranularity;
    off_t offset = (alignedAddr - LB) >> 21; // 2 MB pages

    if (!CAS(&context[offset], UsedHugePage, LockedHugePage)) {
        // Wait for the other thread who owns the lock
        while (context[offset] != SavedHugePage) { }
        return;
    }

    uint64_t *bitmap = (uint64_t *)((char *)view + view->bitmap_offset);
    bitmap += offset * PPBlk / 64; // 8 * sizeof(uint64_t)
    char *src = (char *)alignedAddr;
    char *dst = (char *)view + view->data_offset + (offset << 21);

    // Copy modified 4 KB pages
    for (size_t i = 0; i < (PPBlk >> 6); i++) {
        uint64_t bit = bitmap[i];
        for (off_t p = 0; p < 64; p++) {
            if (bit & 0x0000000000000001) {
                nonTemporalPageCopy(dst, src);
            }
            else {
                nonTemporalCacheLineCopy(dst, src);
            }
            src = src + GlobalAlloc::BitmapGranularity;
            dst = dst + GlobalAlloc::BitmapGranularity;
            bit = bit >> 1;
        }
    }

    _mm_sfence();
    assert(mprotect((void *)alignedAddr, FreeList::BlockSize,
                PROT_READ | PROT_WRITE) == 0);
    assert(CAS(&context[offset], LockedHugePage, SavedHugePage));
}

void Snapshot::blockNewTransactions() {
    for (auto it = NVManager::getInstance().objects.begin();
            it != NVManager::getInstance().objects.end(); it++) {
        it->second->log->snapshot_lock = 1;
    }
    _mm_sfence();
}

void Snapshot::waitForRunningTransactions() {
    bool areThereTransactionsRunning = true;
    NVManager &nvm = NVManager::getInstance();

    while (areThereTransactionsRunning) {
        areThereTransactionsRunning = false;
        for (auto it = nvm.program_threads.begin();
                it != nvm.program_threads.end(); it++) {
            // tx_buffer[0] == number of active transactions
            if (it->second->tx_buffer[0] != 0) {
                areThereTransactionsRunning = true;
                break;
            }
        }
    }
}

void Snapshot::saveAllocationTables() {
    // Save allocation bitmap
    char *bitmap = (char *)view + view->bitmap_offset;
    GlobalAlloc::getInstance()->saveBitmap(bitmap);

    // Save global allocator
    char *snapshot = (char *)view + view->global_offset;
    GlobalAlloc::getInstance()->save(snapshot);

    // Save object allocators
    // No need to lock since all threads are blocked
    snapshot = (char *)view + view->alloc_offset;
    for (auto it = NVManager::getInstance().objects.begin();
            it != NVManager::getInstance().objects.end(); it++) {
        ObjectAlloc *alloc = it->second->alloc;
        *((uint64_t *)snapshot) = it->second->log->last_commit;
        snapshot += sizeof(uint64_t);
        *((uint64_t *)snapshot) = it->second->log->tail;
        snapshot += sizeof(uint64_t);
        *((uintptr_t *)snapshot) = (uintptr_t)it->second;
        snapshot += sizeof(uintptr_t);
        alloc->save(snapshot);
        snapshot += alloc->snapshotSize();
    }

    _mm_sfence();
}

void Snapshot::markPagesReadOnly() {

    GlobalAlloc *instance = GlobalAlloc::getInstance();
    size_t allocatedBlocks = instance->allocatedBlocks();
    uint64_t *bitmap = (uint64_t *)((char *)view + view->bitmap_offset);

    off_t bitmapOffset = 0;
    bool isAllocated;
    size_t regionSize = 0;
    uintptr_t addr = GlobalAlloc::BaseAddress;

    for (size_t b = 0; b < allocatedBlocks; b++) {
        isAllocated = bitmap[bitmapOffset++] > 0;  // 0
        isAllocated |= bitmap[bitmapOffset++] > 0; // 1
        isAllocated |= bitmap[bitmapOffset++] > 0; // 2
        isAllocated |= bitmap[bitmapOffset++] > 0; // 3
        isAllocated |= bitmap[bitmapOffset++] > 0; // 4
        isAllocated |= bitmap[bitmapOffset++] > 0; // 5
        isAllocated |= bitmap[bitmapOffset++] > 0; // 6
        isAllocated |= bitmap[bitmapOffset++] > 0; // 7

        if (isAllocated) {
            context[b] = UsedHugePage;
            regionSize += FreeList::BlockSize;
        }
        else {
            context[b] = FreeHugePage;
            if (regionSize > 0) {
                assert(mprotect((void *)addr, regionSize, PROT_READ) == 0);
                regionSize = 0;
            }
            addr = GlobalAlloc::BaseAddress + (b + 1) * FreeList::BlockSize;
        }
    }

    if (regionSize > 0) {
        assert(mprotect((void *)addr, regionSize, PROT_READ) == 0);
    }
}

void Snapshot::nonTemporalCacheLineCopy(char *dst, char *src) {
    __m128i *dstPtr = (__m128i *)dst;
    __m128i *srcPtr = (__m128i *)src;

    // 64 bytes = 512 bits = 4 * 128 bits
    __m128i xmm0 = _mm_loadu_si128(srcPtr + 0);
    __m128i xmm1 = _mm_loadu_si128(srcPtr + 1);
    __m128i xmm2 = _mm_loadu_si128(srcPtr + 2);
    __m128i xmm3 = _mm_loadu_si128(srcPtr + 3);

    _mm_stream_si128(dstPtr + 0, xmm0);
    _mm_stream_si128(dstPtr + 1, xmm1);
    _mm_stream_si128(dstPtr + 2, xmm2);
    _mm_stream_si128(dstPtr + 3, xmm3);
}

void Snapshot::nonTemporalPageCopy(char *dst, char *src) {
    __m128i *dstPtr = (__m128i *)dst;
    __m128i *srcPtr = (__m128i *)src;

    // 4096 bytes = 32768 bits = 256 x 128 bits = 16 x 16 x 128 bits
    #pragma unroll(16)
    for (int i = 0; i < 16; i++) {
        __m128i xmm0 = _mm_loadu_si128(srcPtr + 0);
        __m128i xmm1 = _mm_loadu_si128(srcPtr + 1);
        __m128i xmm2 = _mm_loadu_si128(srcPtr + 2);
        __m128i xmm3 = _mm_loadu_si128(srcPtr + 3);
        __m128i xmm4 = _mm_loadu_si128(srcPtr + 4);
        __m128i xmm5 = _mm_loadu_si128(srcPtr + 5);
        __m128i xmm6 = _mm_loadu_si128(srcPtr + 6);
        __m128i xmm7 = _mm_loadu_si128(srcPtr + 7);
        __m128i xmm8 = _mm_loadu_si128(srcPtr + 8);
        __m128i xmm9 = _mm_loadu_si128(srcPtr + 9);
        __m128i xmm10 = _mm_loadu_si128(srcPtr + 10);
        __m128i xmm11 = _mm_loadu_si128(srcPtr + 11);
        __m128i xmm12 = _mm_loadu_si128(srcPtr + 12);
        __m128i xmm13 = _mm_loadu_si128(srcPtr + 13);
        __m128i xmm14 = _mm_loadu_si128(srcPtr + 14);
        __m128i xmm15 = _mm_loadu_si128(srcPtr + 15);

        _mm_stream_si128(dstPtr + 0, xmm0); // 16 bytes
        _mm_stream_si128(dstPtr + 1, xmm1);
        _mm_stream_si128(dstPtr + 2, xmm2);
        _mm_stream_si128(dstPtr + 3, xmm3);
        _mm_stream_si128(dstPtr + 4, xmm4);
        _mm_stream_si128(dstPtr + 5, xmm5);
        _mm_stream_si128(dstPtr + 6, xmm6);
        _mm_stream_si128(dstPtr + 7, xmm7);
        _mm_stream_si128(dstPtr + 8, xmm8);
        _mm_stream_si128(dstPtr + 9, xmm9);
        _mm_stream_si128(dstPtr + 10, xmm10);
        _mm_stream_si128(dstPtr + 11, xmm11);
        _mm_stream_si128(dstPtr + 12, xmm12);
        _mm_stream_si128(dstPtr + 13, xmm13);
        _mm_stream_si128(dstPtr + 14, xmm14);
        _mm_stream_si128(dstPtr + 15, xmm15);

        dstPtr += 16;
        srcPtr += 16;
    }
}

void Snapshot::snapshotWorker(off_t offset, size_t length) {

    GlobalAlloc *ga = GlobalAlloc::getInstance();
    const size_t BlockSize = FreeList::BlockSize;
    const size_t PPBlk = BlockSize / ga->BitmapGranularity;
    const size_t BitmapStepSize = PPBlk / 64; // 8 for 2 MB super-pages

    // Setup data and bitmap pointers
    char *dst = (char *)view + view->data_offset + offset * BlockSize;
    char *src = (char *)ga->BaseAddress + offset * BlockSize;
    uint64_t *bitmap = (uint64_t *)((char *)view + view->bitmap_offset);
    bitmap += offset * BitmapStepSize;

    for (off_t sp = 0; sp < length; sp++) {
        if (CAS(&context[offset + sp], UsedHugePage, LockedHugePage)) {

            // Copy modified 4 KB pages
            void *oldSrc = src;
            for (size_t b = 0; b < PPBlk; b += 64) {
                uint64_t bit = *bitmap;
                for (off_t p = 0; p < 64; p++) {
                    if (bit & 0x0000000000000001) {
                        nonTemporalPageCopy(dst, src);
                    }
                    else {
                        // TODO avoid this by filling unused pages at recovery
                        nonTemporalCacheLineCopy(dst, src);
                    }
                    src = src + GlobalAlloc::BitmapGranularity;
                    dst = dst + GlobalAlloc::BitmapGranularity;
                    bit >>= 1;
                }
                bitmap++;
            }

            // Persist changes
            _mm_sfence();

            assert(mprotect(oldSrc, FreeList::BlockSize,
                        PROT_READ | PROT_WRITE) == 0);

            assert(CAS(&context[offset + sp], LockedHugePage, SavedHugePage));
        }
        else {
            // Update pointers (skip by one block)
            src = src + BlockSize;
            dst = dst + BlockSize;
            bitmap = bitmap + BitmapStepSize;
        }
    }
}

void Snapshot::unblockNewTransactions() {
    NVManager &nvm = NVManager::getInstance();
    for (auto it = nvm.objects.begin(); it != nvm.objects.end(); it++) {
        it->second->log->snapshot_lock = 0;
    }
    _mm_sfence();

    pthread_mutex_lock(nvm.ckptLock());
    pthread_cond_broadcast(nvm.ckptCondition());
    pthread_mutex_unlock(nvm.ckptLock());
}

void Snapshot::extendSnapshot(size_t allocatedBlocks) {
    size_t snapshotSize = view->size;
    size_t dataSize = allocatedBlocks * FreeList::BlockSize;
    assert(munmap(view, snapshotSize) == 0);
    assert(fallocate(fd, 0, snapshotSize, dataSize) == 0);
    snapshotSize += dataSize;
    // TODO support for huge-pages
    view = (snapshot_header_t *)mmap(NULL, snapshotSize,
            PROT_READ | PROT_WRITE, MAP_SHARED_VALIDATE | MAP_SYNC, fd, 0);
    assert(view != NULL);

    uintptr_t ptr = (uintptr_t)view + view->data_offset;
    ptr = ptr & ~(4096 - 1);
    assert(madvise((void *)ptr, dataSize, MADV_WILLNEED) == 0);
    view->size = snapshotSize;
}

void Snapshot::saveModifiedPages(size_t allocatedBlocks) {

    // Calculate shares for each snapshot thread
    assert(allocatedBlocks % SnapshotThreads == 0);
    size_t shareLength = allocatedBlocks / SnapshotThreads;

    // Start snapshot threads
    off_t threadIndex = 0;
    vector<std::thread *> threads;
    for (size_t i = 0; i < SnapshotThreads; i++) {
        threads.push_back(new thread(&Snapshot::snapshotWorker, this,
                    threadIndex, shareLength));
        threadIndex += shareLength;
    }

    // Wait for completion
    for (size_t i = 0; i < SnapshotThreads; i++) {
        std::thread *thread = threads.back();
        thread->join();
        threads.pop_back();
        delete thread;
    }
}

void Snapshot::cleanEnvironment() {
    free(context);
    context = NULL;
    size_t snapshotSize = view->size;
    munmap(view, snapshotSize);
    close(fd);
    view = NULL;
    fd = 0;
}

void Snapshot::loadSnapshot(uint32_t id) {
    experimental::filesystem::path poolPath = rootPath;
    poolPath /= "snapshot.";
    poolPath += std::to_string(id);
    PRINT("Loading from snapshot: %s\n", poolPath.c_str());
    fd = open(poolPath.c_str(), O_RDONLY, 0666);
    assert(fd > 0);

    view = (snapshot_header_t *)mmap(NULL, sizeof(snapshot_header_t),
            PROT_READ, MAP_SHARED_VALIDATE | MAP_SYNC, fd, 0);
    assert(view != NULL);
    size_t snapshotSize = view->size;
    munmap(view, sizeof(snapshot_header_t));

    view = (snapshot_header_t *)mmap(NULL, snapshotSize,
            PROT_READ, MAP_SHARED_VALIDATE | MAP_SYNC, fd, 0);
    assert(view != NULL);
    PRINT("Mapped snapshot: %zu bytes at %p\n", snapshotSize, view);
}

// TODO merge the following with snapshotWorker
void Snapshot::restoreWorker(off_t offset, size_t length) {

    GlobalAlloc *ga = GlobalAlloc::getInstance();
    const size_t PagesPerBlock = FreeList::BlockSize / ga->BitmapGranularity;
    const size_t BitmapStepSize = PagesPerBlock / 64;

    char *src = (char *)view + view->data_offset + offset * FreeList::BlockSize;
    char *dst = (char *)(ga->BaseAddress + offset * FreeList::BlockSize);
    uint64_t *bitmap = (uint64_t *)((char *)view + view->bitmap_offset);
    bitmap += offset * BitmapStepSize;

    for (size_t sp = 0; sp < length; sp++) {
        // Skip over unused super-pages (blocks)
        if (bitmap[0] == 0 && bitmap[1] == 0 &&
            bitmap[2] == 0 && bitmap[3] == 0 &&
            bitmap[4] == 0 && bitmap[5] == 0 &&
            bitmap[6] == 0 && bitmap[7] == 0) {

            bitmap += 8;
            src = src + FreeList::BlockSize;
            dst = dst + FreeList::BlockSize;
            continue;
        }

        for (size_t b = 0; b < PagesPerBlock; b += 64) {
            uint64_t bit = *bitmap;
            for (off_t p = 0; p < 64; p++) {
                if (bit & 0x0000000000000001) {
                    nonTemporalPageCopy(dst, src);
                }
                else {
                    nonTemporalCacheLineCopy(dst, src);
                }
                src = src + GlobalAlloc::BitmapGranularity;
                dst = dst + GlobalAlloc::BitmapGranularity;
                bit >>= 1;
            }
            bitmap++;
        }
    }
}

void Snapshot::load(uint32_t id, NVManager *manager) {

    loadSnapshot(id);

    // Global allocator and the bitmap
    const char *bitmap = (const char *)((char *)view + view->bitmap_offset);
    const char *gaCkpt = (const char *)((char *)view + view->global_offset);
    GlobalAlloc *ga = new GlobalAlloc(gaCkpt, bitmap);

    // Data
    const char *data = (const char *)((char *)view + view->data_offset);
    size_t dataSize = view->size - (size_t)view->data_offset;

    // TODO replace this with on-demand paging using userfaultfd(2)
    // Start restore threads
    size_t shareLength = ga->allocatedBlocks() / SnapshotThreads;
    off_t threadIndex = 0;
    vector<std::thread *> threads;
    for (size_t i = 0; i < SnapshotThreads; i++) {
        threads.push_back(new thread(&Snapshot::restoreWorker, this,
                    threadIndex, shareLength));
        threadIndex += shareLength;
    }

    // Wait for completion
    for (size_t i = 0; i < SnapshotThreads; i++) {
        std::thread *thread = threads.back();
        thread->join();
        threads.pop_back();
        delete thread;
    }
    PRINT("Finished restoring pages from snapshot\n");

    // Persistent objects and allocators
    std::map<std::string, uint64_t> lastCommitIDs;
    char *objCkpt = (char *)view + view->alloc_offset;
    for (uint32_t i = 0; i < view->object_count; i++) {
        uint64_t lastCommit = *((uint64_t *)objCkpt);
        objCkpt += sizeof(uint64_t);
        uint64_t logTail = *((uint64_t *)objCkpt);
        objCkpt += sizeof(uint64_t);
        uintptr_t objectPtr = *((uintptr_t *)objCkpt);
        objCkpt += sizeof(uintptr_t);
        PRINT("Recovering object at %p, last commit = %zu, and log tail = %zu\n",
                (void*)objectPtr, lastCommit, logTail);

        uuid_t uuid;
        memcpy(uuid, objCkpt, sizeof(uuid_t));
        void *allocPtr = (void *)(((uint64_t *)objCkpt)[3]);
        ObjectAlloc *alloc = new (allocPtr) ObjectAlloc(uuid, objCkpt);
        ga->restoreAllocator(alloc);
        objCkpt += alloc->snapshotSize();

        if (manager != NULL) {
            char uuid_str[64];
            uuid_unparse(uuid, uuid_str);
            manager->objects.insert(pair<string, PersistentObject *>(
                        uuid_str, (PersistentObject *)objectPtr));
            lastCommitIDs.insert(pair<string, uint64_t>(uuid_str, lastCommit));
            RecoveryContext::getInstance().pushLogHeadOffset(uuid_str, logTail);
        }
    }
    PRINT("Finished restoring allocators for %d object(s)\n", view->object_count);

    // Fix unused huge-pages (free blocks)
    objCkpt = (char *)view + view->alloc_offset;
    for (uint32_t i = 0; i < view->object_count; i++) {
        objCkpt += sizeof(uint64_t); // last commit
        objCkpt += sizeof(uint64_t); // log tail
        objCkpt += sizeof(uintptr_t); // object pointer

        uuid_t uuid;
        memcpy(uuid, objCkpt, sizeof(uuid_t));
        ObjectAlloc *alloc = ga->findAllocator(uuid);
        assert(alloc != NULL);
        alloc->releaseFreeBlocks();
        objCkpt += alloc->snapshotSize();
    }

    cleanEnvironment();
    if (manager == NULL) return;

    // Reset last played commit IDs
    for (auto it = manager->objects.begin();
            it != manager->objects.end(); it++) {
        assert(lastCommitIDs.find(it->first) != lastCommitIDs.end());
        uint64_t commitID = lastCommitIDs.find(it->first)->second;
        it->second->last_played_commit_id = commitID;
    }
}

#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <experimental/filesystem>

using namespace std;
class NVManager;

typedef struct {
    uint32_t identifier;
    uint32_t object_count;
    uint64_t time;
    uint64_t size;
    uint32_t sync_latency;
    uint32_t async_latency;
    off_t bitmap_offset;
    off_t global_offset;
    off_t alloc_offset;
    off_t data_offset;
} snapshot_header_t;

namespace {
    class SnapshotTestSuite;
}

class Snapshot {
public:
    Snapshot(const char *);
    ~Snapshot();
    static Snapshot *getInstance();
    static bool anyActiveSnapshot();
    uint32_t create();
    void snapshotWorker(off_t, size_t);
    void restoreWorker(off_t, size_t);
    void load(uint32_t id = 0, NVManager *manager = NULL);
    void pageFaultHandler(void *);
    uint32_t lastSnapshotID();

protected:
    void loadSnapshot(uint32_t);
    void prepareSnapshot();
    void blockNewTransactions();
    void unblockNewTransactions();
    void waitForRunningTransactions();
    void saveAllocationTables();
    void extendSnapshot(size_t);
    void saveModifiedPages(size_t);
    void cleanEnvironment();
    void markPagesReadOnly();
    void nonTemporalPageCopy(char *, char *);
    void nonTemporalCacheLineCopy(char *, char *);
    void getExistingSnapshots(std::vector<uint32_t>&);
    void waitForFaultHandlers(size_t);

private:
    static Snapshot *instance;
    experimental::filesystem::path rootPath;
    int fd;
    snapshot_header_t *view;
    uint64_t *context;

    friend class ::SnapshotTestSuite;

public:
    const size_t SnapshotThreads = 4;
    const uint64_t UsedHugePage = 0xAAAAAAAAAAAAAAAA;
    const uint64_t FreeHugePage = 0xFFFFFFFFFFFFFFFF;
    const uint64_t LockedHugePage = 0xAFAFAFAFAFAFAFAF;
    const uint64_t SavedHugePage = 0x0000000000000000;
    static_assert(sizeof(snapshot_header_t) == 64,
            "Snapshot header is not cache-aligned!");
};

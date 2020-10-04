#include <stdint.h>
#include <stdio.h>
#include <cstring>
#include <queue>
#include "nv_object.hpp"
#include "nv_log.hpp"
#include "savitar.hpp"
#include "nvm_manager.hpp"
#include "recovery_context.hpp"

/*
 * Constructor is only called for new objects:
 * * The first time a persistent object is constructed
 * * Every time a persistent object is solely constructed from logs
 * * Constructor is never called when snapshots are used
 */
PersistentObject::PersistentObject(uuid_t id) {
    constructor(id);
}

void PersistentObject::constructor(uuid_t id) {
    if (id == NULL) {
        uuid_t tid;
        uuid_generate(tid);
        id = tid;
    }
    alloc = GlobalAlloc::getInstance()->findAllocator(id);
    assert(alloc != NULL);
    uuid_copy(uuid, id);
    uuid_unparse(id, uuid_str);

    if (Savitar_log_exists(uuid)) {
        log = Savitar_log_open(uuid);
    }
    else {
        log = Savitar_log_create(uuid, LOG_SIZE);
    }
}

PersistentObject::PersistentObject(bool dummy) {
    if (!dummy) constructor(NULL);
}

PersistentObject::~PersistentObject() {
    if (log == NULL) return; // handle dummy objects
    // TODO handle re-assignment of objects (remap semantic log)
    Savitar_log_close(log);
    NVManager &manager = NVManager::getInstance();
    manager.lock();
    manager.destroy(this);
    manager.unlock();
}

class CommitRecord {
    public:
        CommitRecord(char *ptr, uint64_t commit_id, uint64_t method_tag) {
            _ptr = ptr;
            _commit_id = commit_id;
            _method_tag = method_tag;
        }

        uint64_t getCommitId() const { return _commit_id; }
        uint64_t getMethodTag() const { return _method_tag; }
        char *getPtr() const { return _ptr; }

    private:
        char *_ptr;
        uint64_t _commit_id;
        uint64_t _method_tag;
};

inline bool operator< (const CommitRecord& lhs, const CommitRecord& rhs) {
    return lhs.getCommitId() > rhs.getCommitId(); // Force ASC order for priority queue
}

/*
 * [General rules]
 * NVM manager is responsible for recovering all persistent objects through calling their Recover()
 * method at startup. Each object is assigned to a recovery thread. Look for the constructor method of
 * NVManager for more details.
 * Also, all allocations are handled by the NVM manager object, which either finds the object or creates
 * an new persistent object using the object's factory method.
 * ----------------------------------------------------------------------------------------------------
 * [Single object recovery]
 * If there are no dependencies (nested transactions), the object will go through the log and plays
 * log entries one by one based on the commit order.
 * ----------------------------------------------------------------------------------------------------
 * [Normal recovery]
 * In presence of dependant objects, here is how the synchronization between persistent objects works to
 * ensure the right replay order for log entries:
 * > Parent: there is no change in the recovery code except the code snippet which child objects run to
 *   make sure the method on child object is called (and executed) at the right order with respect to
 *   other operations. In other words, the child object will stall the calls until it's ready.
 * > Child (non-parent): once a child object reads a log entry that belongs to a nested transaction,
 *   it will wait for the parent object to pass the point specified in the nested transaction log entry.
 *   For example, if the parent log shows commit order 12, the child object waits for the parent to finish
 *   executing the corresponding log entry and update 'last_played_commit_order' to 12.
 * ----------------------------------------------------------------------------------------------------
 * [Partial commits]
 * These are committed log entries for nested transactions where the system fails before marking the
 * outer-most transaction as committed. For example, assume A calls B and the program terminates after
 * B's log entry is marked as committed but before A's entry is marked as committed. In this situation,
 * B must avoid waiting for A and should end the recovery process.
 * If there are other objects trying to play a log entry that involves the child or parent object, the
 * recovery process for those objects should stop as well.
 * If an unclean shutdown is detected, the NVM manager will initiate a process to fix the log so that
 * uncommitted transactions are not played.
 * ----------------------------------------------------------------------------------------------------
 */
void PersistentObject::Recover() {
    assert(log != NULL);
    assert(sizeof(uint64_t) == 8); // We assume 2 * sizeof(uint64_t) == 16
    NVManager *manager = RecoveryContext::getInstance().getManager();
    assert(manager != NULL);

    // Calculating head and limit pointers
    uint64_t logHead = RecoveryContext::getInstance().queryLogHeadOffset(uuid_str);
    if (logHead == 0) logHead = log->head;
    char *ptr = (char *)log + logHead;
    const char *limit = (char *)log + log->tail;

    char uuid_str[64], uuid_prefix[9];
    uuid_unparse(uuid, uuid_str);
    memcpy(uuid_prefix, uuid_str, 8);
    uuid_prefix[8] = '\0';
    PRINT("[%s] Started recovering %s\n", uuid_prefix, uuid_str);
    PRINT("[%s] Log head: %zu\n", uuid_prefix, log->head);
    PRINT("[%s] New head: %zu\n", uuid_prefix, logHead);
    PRINT("[%s] Log tail: %zu\n", uuid_prefix, log->tail);

    // Creating data-structures to handle out-of-order entries
    std::priority_queue<CommitRecord> commit_queue;

    while (ptr < limit) {
        // 1. Read commit id and method tag from persistent log
        uint64_t commit_id = *((uint64_t *)ptr);
        PRINT("[%s] Found record with commit order = %zu\n",
                uuid_prefix, commit_id);
        ptr += sizeof(uint64_t);

        uint64_t magic = *((uint64_t *)ptr);
        if (commit_id == 0 && magic != REDO_LOG_MAGIC) { // partial transaction
            do {
                ptr += CACHE_LINE_WIDTH;
                magic = *((uint64_t *)ptr);
            } while (magic != REDO_LOG_MAGIC);
        }
        // TODO bug fix: update commit_id when skipping partial transactions

        ptr += sizeof(uint64_t);
        assert(magic == REDO_LOG_MAGIC);
        uint64_t method_tag = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);

        // 2. Add the entry to priority queue to sort entries based on commit id
        size_t bytes_processed = sizeof(uuid_t);
        if ((method_tag & NESTED_TX_TAG) == 0) { // dry run
            bytes_processed = Play(method_tag, (uint64_t *)ptr, true);
        }

        if (commit_id > last_played_commit_id) {
            commit_queue.push(CommitRecord(ptr, commit_id, method_tag));
        }

        // 3. Update iterator to point to the next entry
        ptr += bytes_processed;
        ptr += CACHE_LINE_WIDTH - ((24 + bytes_processed) % CACHE_LINE_WIDTH);

        // 4. Use the priority queue to play entries in order
        while (!commit_queue.empty() &&
                commit_queue.top().getCommitId() == last_played_commit_id + 1) {
            const CommitRecord &record = commit_queue.top();
            PRINT("[%s] Playing record with commit order = %zu\n",
                    uuid_prefix, record.getCommitId());
            if (record.getMethodTag() & NESTED_TX_TAG) { // dependant (nested) transaction
                off_t parent_offset = (off_t)(record.getMethodTag() & (~NESTED_TX_TAG));
                PRINT("[%s] Nested transaction, parent entry at offset %zu\n",
                        uuid_prefix, parent_offset);
                struct NestedEntry {
                    uuid_t uuid;
                } *parent_uuid = (struct NestedEntry *)record.getPtr();
                char parent_uuid_str[64];
                uuid_unparse(parent_uuid->uuid, parent_uuid_str);
                PersistentObject *parent = manager->findObject(parent_uuid_str);
                assert(parent != NULL);
                uint64_t expected_commit_id = *((uint64_t *)((char *)parent->log +
                            parent_offset));
                PRINT("[%s] Nested transaction, waiting for object %s to execute commit %zu\n",
                        uuid_prefix, parent_uuid_str, expected_commit_id);
                waitForParent(parent, expected_commit_id);
                while (parent->last_played_commit_id < expected_commit_id) {
                    assert(parent->isRecovering());
                }
                PRINT("[%s] Done waiting for parent object\n", uuid_prefix);
            }
            else {
                Play(record.getMethodTag(), (uint64_t *)record.getPtr(), false);
            }
            last_played_commit_id = record.getCommitId();
            PRINT("[%s] Finished playing commit order %zu, last played commit updated to %zu\n",
                    uuid_prefix, record.getCommitId(), last_played_commit_id);
            commit_queue.pop();
        }
    }

    assert(commit_queue.empty());
    PRINT("[%s] Finished recovering %s\n", uuid_prefix, uuid_str);
}

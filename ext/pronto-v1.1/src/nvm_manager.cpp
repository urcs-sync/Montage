#include <pthread.h>
#include <list>
#include <queue>
#include "nv_factory.hpp"
#include "nv_object.hpp"
#include "nvm_manager.hpp"
#include "nv_catalog.hpp"
#include "recovery_context.hpp"
#include "snapshot.hpp"

using namespace std;

NVManager::NVManager() {
    PRINT("Initializing manager object\n");
    pthread_mutex_init(&_lock, NULL);
    pthread_mutex_init(&_ckptLock, NULL);
    pthread_cond_init(&_ckptCondition, NULL);

    /*
     * Reading catalog from NVM -- this will populate ex_objects
     * The catalog contains the superset of objects in any snapshot
     */
    list< pair<std::string, CatalogEntry *> > ex_objects;
    catalog = new NVCatalog(CATALOG_FILE_NAME, ex_objects);
    PRINT("Finished creating/opening persistent catalog.\n");
    if (ex_objects.size() == 0) return;

    // Prepare for recovery
    RecoveryContext::getInstance().setManager(this);
    struct timespec t1, t2;
    clock_gettime(CLOCK_REALTIME, &t1);

    // Load the latest snapshot (if any)
    Snapshot *snapshot = new Snapshot(PMEM_PATH);
    if (snapshot->lastSnapshotID() > 0) {
        snapshot->load(snapshot->lastSnapshotID(), this);
    }
    delete snapshot;

    // Prepare environment for recovery (populate objects from ex_objects)
    for (auto it = ex_objects.begin(); it != ex_objects.end(); ++it) {
        recoverObject(it->first.c_str(), it->second);
    }
    ex_objects.clear();
    const size_t obj_count = objects.size();

    /*
     * Handling unclean shutdowns
     * We provide support for transaction aborts here. The effect of an aborted
     * transaction on other objects/transactions is implemented here.
     */
    uint64_t cflags = catalog->getFlags();
    if ((cflags & CatalogFlagCleanShutdown) == 0) {
        PRINT("Manager: detected unclean shutdown, fixing redo-logs ...\n");
        int aborted_transactions = emulateRecoveryAndFixRedoLogs();
        PRINT("Manager: finished fixing redo-logs, total aborted transactions = %d\n",
                aborted_transactions);
    }

    /*
     * Creating recovery threads (one per persistent object)
     * We assume number of persistent objects are not much larger than
     * the number of CPU cores.
     * If that is not the case, we should partition persistent objects to
     * groups with no inter-class dependencies and recover partitions
     * one at a time.
     */
    PRINT("Manager: recovering persistent objects ...\n");
    size_t counter = 0;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * obj_count);
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        assert(counter < obj_count);
        pthread_create(&threads[counter++], NULL, recoveryWorker, it->second);
    }

    // Wait for recovery threads to return
    for (size_t i = 0; i < obj_count; i++) {
        pthread_join(threads[i], NULL);
    }

    // Cleanup environment
    clock_gettime(CLOCK_REALTIME, &t2);
    PRINT("Manager: finished recovering persistent objects!\n");
    free(threads);

    PRINT("Manager: updating catalog flags.\n");
    cflags = catalog->getFlags();
    cflags = (cflags & (~CatalogFlagCleanShutdown)); // unclean shutdown
    catalog->setFlags(cflags);

    uint64_t recoveryTime = (t2.tv_sec - t1.tv_sec) * 1E9;
    recoveryTime += (t2.tv_nsec - t1.tv_nsec);
    fprintf(stdout, "Recovery Time (ms)\t%.2f\n", (double)recoveryTime / 1E6);
}

NVManager::~NVManager() {
    PRINT("Manager: updating catalog flags before terminating.\n");
    uint64_t cflags = catalog->getFlags();
    cflags = cflags | CatalogFlagCleanShutdown;
    catalog->setFlags(cflags);

    delete catalog;
    pthread_mutex_destroy(&_lock);
    pthread_mutex_destroy(&_ckptLock);
    pthread_cond_destroy(&_ckptCondition);
    PRINT("Destroyed manager object\n");
#ifdef DEBUG
    GlobalAlloc::getInstance()->report();
#endif
}

PersistentObject *NVManager::findRecovered(uuid_t uuid) {
    char uuid_str[64];
    uuid_unparse(uuid, uuid_str);
    auto it = objects.find(uuid_str);
    if (it == objects.end()) return NULL;
    PersistentObject *object = it->second;
    assert(!object->assigned);
    object->assigned = true;
    return object;
}

void NVManager::createNew(uint64_t type_id, PersistentObject *object) {
    CatalogEntry *entry = catalog->add(object->getUUID(), type_id);
    if (object->const_args != NULL) {
        catalog->addConstructorArgs(entry, object->const_args,
                object->const_args_size);
    }
    char uuid_str[64];
    uuid_unparse(object->getUUID(), uuid_str);
    objects.insert(pair<string, PersistentObject *>(uuid_str, object));
    object->assigned = true;
}

void NVManager::destroy(PersistentObject *object) {
    assert(object->assigned);
    object->assigned = false;
}

void NVManager::recoverObject(const char *uuid_str, CatalogEntry *object) {
    if (objects.find(uuid_str) != objects.end()) { // Recover from snapshot
        PRINT("Recovering object from snapshot, uuid = %s\n", uuid_str);
        PersistentObject *pobj = objects.find(uuid_str)->second;
        assert(pobj != NULL);
        PersistentFactory::vTableUpdate(object->type, pobj);
        PRINT("Updated vTable to %p for persistent object, uuid = %s\n",
                (void*)(((uintptr_t*)pobj)[0]), uuid_str);
        pobj->recovering = true;
        pobj->log = Savitar_log_open(pobj->uuid);
        pobj->alloc = GlobalAlloc::getInstance()->findAllocator(pobj->uuid);
        pobj->assigned = false;
    }
    else {
        PRINT("Adding object to recovery queue, uuid = %s\n", uuid_str);
        PersistentObject *pobj = PersistentFactory::create(this,
                object->type, object);
        assert(pobj != NULL);
        pobj->recovering = true;
        pobj->last_played_commit_id = 0;
        objects.insert(pair<string, PersistentObject *>(uuid_str, pobj));
    }
}

void *NVManager::recoveryWorker(void *arg) {
    PersistentObject *object = (PersistentObject *)arg;
    object->Recover();
    object->recovering = false;
    return NULL;
}

PersistentObject *NVManager::findObject(string uuid_str) {
    auto it = objects.find(uuid_str);
    if (it == objects.end()) {
        PRINT("Unable to find object with uuid = %s\n", uuid_str.c_str());
        PRINT("Total objects present: %zu\n", objects.size());
        return NULL;
    }
    return it->second;
}

const char *NVManager::getArgumentPointer(CatalogEntry *entry) {
    return (const char *)catalog->catalog + entry->args_offset;
}

typedef struct AbortChainNode {
    PersistentObject *object;
    uint64_t log_offset;
    uint64_t commit_id;
    struct AbortChainNode *next;
} AbortChainNode;

typedef struct {
    pthread_t thread;
    NVManager *instance;
    PersistentObject *object;
    uint64_t max_committed_tx;
    list<AbortChainNode *> *abort_chains;
} AbortChainBuilderArg;

void *NVManager::buildAbortChains(void *arg) {
    NVManager *me = ((AbortChainBuilderArg *)arg)->instance;
    PersistentObject *object = ((AbortChainBuilderArg *)arg)->object;
    SavitarLog *log = object->log;
    priority_queue<uint64_t, vector<uint64_t>, greater<uint64_t> > min_heap;


    uint64_t max_committed_tx = 0;
#ifndef PRONTO_BUF
    uint64_t offset = log->head;
#else
    uint64_t offset = sizeof(SavitarLog);
#endif
    const char *data = (const char *)object->log;
    while (offset < log->tail) {
        assert(offset % CACHE_LINE_WIDTH == 0);
        uint64_t commit_id = *((uint64_t *)&data[offset]);
        offset += sizeof(uint64_t);
        uint64_t magic = *((uint64_t *)&data[offset]);
        offset += sizeof (uint64_t);
        uint64_t method_tag = *((uint64_t *)&data[offset]);
        offset += sizeof(uint64_t);

        if (magic != REDO_LOG_MAGIC) { // Corrupted log entry
            offset += CACHE_LINE_WIDTH - 3 * sizeof(uint64_t);
            continue;
        }

        if (commit_id != 0) {
            min_heap.push(commit_id);
            while (min_heap.top() == max_committed_tx + 1) {
                max_committed_tx++;
                min_heap.pop();
            }
        }

        if ((method_tag & NESTED_TX_TAG) == 0) {
            assert(method_tag != 0);
            offset += object->Play(method_tag,
                    (uint64_t *)&data[offset], true); // dry run
            offset += CACHE_LINE_WIDTH - offset % CACHE_LINE_WIDTH;
            continue;
        }

        /*
         * Nested transaction
         * [1] commit_id == 0: no need to follow the chain
         * [2] commit_id != 0: follow the chain and check if aborted
         */
        if (commit_id == 0) {
            offset += CACHE_LINE_WIDTH - 3 * sizeof(uint64_t);
            continue;
        }

        typedef struct uuid_ptr { uuid_t uuid; } uuid_ptr;

        // Creating abort chain
        AbortChainNode *head = (AbortChainNode *)malloc(sizeof(AbortChainNode));
        head->object = object;
        head->commit_id = commit_id;
        head->log_offset = offset - 3 * sizeof(uint64_t);
        head->next = NULL;

        char uuid_str[37];
        uuid_unparse(((uuid_ptr *)&data[offset])->uuid, uuid_str);
        auto parent_it = me->objects.find(uuid_str);
        assert(parent_it != me->objects.end());
        PersistentObject *parent = parent_it->second;
        uint64_t parent_offset = method_tag & (~NESTED_TX_TAG);

        /*
         * Note: we always know the log entry for parent transactions are persistent
         * before the child transactions, so it is safe to assume parent logs are
         * not corrupted (i.e., partially persisted).
         */
        while (parent != NULL) {

            SavitarLog *parent_log = parent->log;
            uint64_t *parent_ptr = (uint64_t *)((char *)parent_log + parent_offset);
            uint64_t parent_commit_id = parent_ptr[0];
            uint64_t parent_magic = parent_ptr[1];
            if (parent_magic != REDO_LOG_MAGIC) {
                PRINT("Invalid magic: (uuid, commit id, offset) = (%s, %zu, %zu)\n",
                        parent->uuid_str, parent_commit_id, parent_offset);
            }
            assert(parent_magic == REDO_LOG_MAGIC);
            uint64_t parent_method_tag = parent_ptr[2];

            // Adding parent to the chain
            AbortChainNode *node = (AbortChainNode *)malloc(sizeof(AbortChainNode));
            node->next = head;
            node->object = parent;
            node->log_offset = parent_offset;
            node->commit_id = parent_commit_id;
            head = node;

            if ((parent_method_tag & NESTED_TX_TAG) == 0) { // outer-most transaction
                if (parent_commit_id == 0) { // aborted transaction
                    ((AbortChainBuilderArg *)arg)->abort_chains->push_back(head);
                }
                else {
                    // Chain clean-up
                    while (head != NULL) {
                        AbortChainNode *t = head;
                        head = head->next;
                        free(t);
                    }
                }
                parent = NULL;
            }
            else {
                uuid_unparse(((uuid_ptr *)((char *)parent_ptr + 24))->uuid, uuid_str);
                auto parent_it = me->objects.find(uuid_str);
                assert(parent_it != me->objects.end());
                parent = parent_it->second;
                parent_offset = parent_method_tag & (~NESTED_TX_TAG);
            }
        }

        offset += CACHE_LINE_WIDTH - 3 * sizeof(uint64_t);
    }

    ((AbortChainBuilderArg *)arg)->max_committed_tx = max_committed_tx;
    return NULL;
}

int NVManager::emulateRecoveryAndFixRedoLogs() {

    size_t counter = 0;
    const size_t obj_count = objects.size();
    AbortChainBuilderArg *builders = (AbortChainBuilderArg *)malloc(obj_count *
            sizeof(AbortChainBuilderArg));

    PRINT("Manager: building abort chains for %zu objects.\n", obj_count);
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        assert(counter < obj_count);
        builders[counter].instance = this;
        builders[counter].object = it->second;
        builders[counter].abort_chains = new list<AbortChainNode *>();
        pthread_create(&builders[counter].thread, NULL, buildAbortChains,
                &builders[counter]);
        counter++;
    }

    // Wait for recovery threads to return
    for (size_t i = 0; i < obj_count; i++) {
        pthread_join(builders[i].thread, NULL);
        AbortChainNode *chain = NULL;
        PRINT("Manager: processing abort chain for %s (max commit = %zu)\n",
                builders[i].object->uuid_str, builders[i].max_committed_tx);
        if (!builders[i].abort_chains->empty()) {
            PRINT("Aborted nested transactions are not yet supported!");
        }
        assert(builders[i].abort_chains->empty());
        /*
        while (!builders[i].abort_chains->empty()) {
            PRINT("\t> Found abort chain:");
            chain = builders[i].abort_chains->front();
            while (chain != NULL) {
                // Process the abort node
                PRINT("\t\t> %s : (%zu, %zu)\n", chain->object->uuid_str,
                        chain->log_offset, chain->commit_id);
                // TODO process chain

                // Cleanup
                AbortChainNode *t = chain;
                chain = chain->next;
                free(t);
            }
            builders[i].abort_chains->pop_front();
        }
        */
    }
    PRINT("Manager: finished building abort chains.\n");

    // TODO
    free(builders);
    return 0;
}
void NVManager::registerThread(pthread_t thread, ThreadConfig *cfg) {
    program_threads[thread] = cfg;
}

void NVManager::unregisterThread(pthread_t thread) {
    program_threads.erase(thread);
}

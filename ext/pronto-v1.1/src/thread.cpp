#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "thread.hpp"
#include "persister.hpp"
#include "nvm_manager.hpp"
#include "recovery_context.hpp"

void get_cpu_info(uint8_t *core_map, int *map_size);

static int available_cores = 80; // including Hyper-Threaded cores
static uint16_t core_tenants[MAX_CORES / 2];
static uint8_t core_ht_map[MAX_CORES / 2][2];
static pthread_mutex_t core_tenants_lock;

void Savitar_core_init() {
    uint8_t core_info[MAX_CORES];
    get_cpu_info(core_info, &available_cores);
    assert(available_cores <= MAX_CORES);
    PRINT("Found a total of %d active cores.\n", available_cores);
    assert(available_cores % 2 == 0);

    PRINT("Zeroing core affinity data structures for %d physical cores.\n",
            available_cores / 2);
    for (int i = 0; i < (available_cores >> 1); i++) {
        core_tenants[i] = 0; // physical cores
    }
    assert(pthread_mutex_init(&core_tenants_lock, NULL) == 0);

    // Create HT to Physical mapping
    uint8_t cores_per_socket = 0; // assumes identical CPUs
    PRINT("Printing Hyper-Threading map\n");
    for (uint8_t c = 0; c < (available_cores >> 1); c++) {
        for (uint8_t ht = 0; ht < 2; ht++) { // number of hyper-threads
            int min_offset = 0;
            for (int i = 1; i < available_cores; i++) {
                if (core_info[i] < core_info[min_offset])
                    min_offset = i;
            }
            core_ht_map[c][ht] = min_offset; // processor id
            if (ht == 0 && core_info[min_offset] < 32) cores_per_socket++;
            core_info[min_offset] = 0xFF;
        }
        PRINT("Core %d = { %d, %d }\n", c, core_ht_map[c][0], core_ht_map[c][1]);
    }
    PRINT("Total of %d physical cores per socket\n", cores_per_socket);

#ifdef NO_HT_PINNING
    PRINT("HT-pinning is disabled, updating the HT-map\n");
    uint8_t sockets = (available_cores / 2) / cores_per_socket;
    for (uint8_t s = 0; s < sockets; s++) {
        PRINT("Socket %d\n", s);
        uint8_t b = s * cores_per_socket;
        for (uint8_t c = 0; c < cores_per_socket / 2; c++) {
            std::swap(core_ht_map[b + c][1],
                    core_ht_map[b + c + cores_per_socket / 2][0]);
        }
        for (uint8_t c = 0; c < cores_per_socket; c++) {
            PRINT("Core %d = { %d, %d }\n", c,
                    core_ht_map[b + c][0], core_ht_map[b + c][1]);
        }
    }
#endif
}

void Savitar_core_finalize() {
    // for (int i = 0; i < (available_cores >> 1); i++) {
    //     while (core_tenants[i] != 0); // wait for all threads to terminate
    // }
    assert(pthread_mutex_destroy(&core_tenants_lock) == 0);
}
// thread pinning strategy for 2x20a:
// 1 thread per core on one socket -> hyperthreads on the same socket -> cross socket.
static const int pinning_map_2x20a_1[] = {
    0,2,4,6,8,10,12,14,16,18,
    20,22,24,26,28,30,32,34,36,38,
    40,42,44,46,48,50,52,54,56,58,
    60,62,64,66,68,70,72,74,76,78,
    1,3,5,7,9,11,13,15,17,19,
    21,23,25,27,29,31,33,35,37,39,
    41,43,45,47,49,51,53,55,57,59,
    61,63,65,67,69,71,73,75,77,79};
volatile static int init_count = 0;
void Savitar_core_alloc(int *core_ids) {
    int least_occupied = 0;
    assert(pthread_mutex_lock(&core_tenants_lock) == 0);
// #ifndef SYNC_SL
    int total_cores = available_cores;
// #else // SYNC_SL
//     int total_cores = available_cores >> 1;// force to reuse cores within socket
// #endif // SYNC_SL
    for (int i = 0; i < (total_cores >> 1); i++) {
        if (core_tenants[i] < core_tenants[least_occupied]) {
            least_occupied = i;
        }
    }
    core_tenants[least_occupied]++;
    int curr_tenants=core_tenants[least_occupied];
    int tid = init_count;
    init_count++;
    assert(pthread_mutex_unlock(&core_tenants_lock) == 0);
    // core_ids[0] = core_ht_map[least_occupied][0];
#ifndef SYNC_SL
    // core_ids[1] = core_ht_map[least_occupied][1];
    assert(tid<40);
    if(tid>=20) tid+=20;
    core_ids[0] = pinning_map_2x20a_1[(tid+20)%80]; // logger
    core_ids[1] = pinning_map_2x20a_1[tid%80];
#else // SYNC_SL
    // if(curr_tenants%2==1){
    //     core_ids[1] = core_ht_map[least_occupied][1];
    // } else {
    //     core_ids[1] = core_ht_map[least_occupied][0];
    // }
    core_ids[0] = 0; //unused
    core_ids[1] = pinning_map_2x20a_1[tid%80];
#endif // SYNC_SL

    PRINT("Adding new tenants to cores %d and %d.\n",
        core_ids[0], core_ids[1]);
}

// Must only be called once by either the logger or main thread
void Savitar_core_free(int core_id) {
    assert(core_id >= 0);
    assert(core_id < available_cores);
    assert(pthread_mutex_lock(&core_tenants_lock) == 0);

    uint8_t physical_core_id = UINT8_MAX;
    for (uint8_t c = 0; c < (available_cores >> 1); c++) {
        if (core_id == core_ht_map[c][0] || core_id == core_ht_map[c][1]) {
            physical_core_id = c;
            break;
        }
    }
    assert(physical_core_id != UINT8_MAX);

    core_tenants[physical_core_id]--;
    assert(pthread_mutex_unlock(&core_tenants_lock) == 0);
    PRINT("Removing tenants from cores %d and %d.\n",
        core_ht_map[physical_core_id][0], core_ht_map[physical_core_id][1]);
}

/*
 * tx_buffer[0] is used to index sync_buffer
 */
static __thread NvMethodCall *sync_buffer;

/*
 * Contains offset of redo-log entries for active transactions
 * tx_buffer[0]: number of active transactions for current thread
 * tx_buffer[1+]: redo-log offset
 */
static __thread uint64_t *tx_buffer;

static void *routine_wrapper(void *arg) {

    // Prepare environment
    ThreadConfig *cfg = (ThreadConfig *)arg;
    sync_buffer = cfg->buffer;
    tx_buffer = cfg->tx_buffer;

    // Set thread core affinity
    pthread_t thread = pthread_self();
// #ifndef SYNC_SL
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cfg->core_id, &cpuset);
    assert(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == 0);
// #endif // SYNC_SL

    // Wait for thread routine to return
    void *ret_val = cfg->routine(cfg->argument);

    // Clean up
    if (cfg->routine == Savitar_persister_worker) {
        PRINT("[%d] Persister thread is now terminating\n", (int)thread);
        free(cfg->buffer);
        free(cfg->tx_buffer);
        free(cfg->argument);
    }
    else { // main thread
        PRINT("[%d] Worker thread is now terminating\n", (int)thread);
        NVManager::getInstance().lock();
        NVManager::getInstance().unregisterThread(pthread_self());
        NVManager::getInstance().unlock();
        assert(tx_buffer[0] == 0); // No active transactions
        cfg->buffer[0].method_tag = UINT64_MAX; // Signals logger thread to terminate
// #ifndef SYNC_SL
        Savitar_core_free(cfg->core_id);
// #endif // SYNC_SL
#ifdef SYNC_SL
        free(cfg->buffer);
        free(cfg->tx_buffer);
#endif // SYNC_SL
    }
    free(cfg);

    return ret_val;
}

int Savitar_thread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*start_routine)(void *), void *arg) {

    // Allocate shared buffer
    NvMethodCall *buffer = (NvMethodCall *)calloc(MAX_ACTIVE_TXS, sizeof(NvMethodCall));
    assert(buffer != NULL);
    memset(buffer, 0, sizeof(NvMethodCall) * MAX_ACTIVE_TXS);

    // Allocate transaction buffer
    uint64_t *tx_buffer = (uint64_t *)calloc(MAX_ACTIVE_TXS + 1, sizeof(uint64_t));
    assert(tx_buffer != NULL);
    memset(tx_buffer, 0, sizeof(uint64_t) * (MAX_ACTIVE_TXS + 1));

    // Get cores which host main and logger threads
    int core_ids[2];
    Savitar_core_alloc(core_ids);
#ifndef SYNC_SL
    // Create logger thread configuration
    ThreadConfig *logger_cfg = (ThreadConfig *)malloc(sizeof(ThreadConfig));
    logger_cfg->core_id = core_ids[0];
    logger_cfg->buffer = buffer;
    logger_cfg->tx_buffer = tx_buffer;
    logger_cfg->routine = Savitar_persister_worker;
    TxBuffers *tx_buffers = (TxBuffers *)malloc(sizeof(TxBuffers));
    tx_buffers->buffer = buffer;
    tx_buffers->tx_buffer = tx_buffer;
    logger_cfg->argument = tx_buffers;

    // Create the logger thread
    pthread_t logger_thread;
    int r1 = pthread_create(&logger_thread, NULL, routine_wrapper, logger_cfg);
    assert(r1 == 0);
#endif // !SYNC_SL

    // Create main thread configuration
    ThreadConfig *main_cfg = (ThreadConfig *)malloc(sizeof(ThreadConfig));
    main_cfg->core_id = core_ids[1];
    main_cfg->buffer = buffer;
    main_cfg->tx_buffer = tx_buffer;
    main_cfg->routine = start_routine;
    main_cfg->argument = arg;

    // Create the main thread
    int r2 = pthread_create(thread, attr, routine_wrapper, main_cfg);
    assert(r2 == 0);
#ifndef SYNC_SL
    tx_buffers->thread_id = (int)*thread;
#endif // SYNC_SL
    NVManager::getInstance().lock();
    NVManager::getInstance().registerThread(*thread, main_cfg);
    NVManager::getInstance().unlock();

    return r2;
}

#ifdef DEBUG
static __thread uint64_t cycles[4];

static inline uint64_t rdtscp() {
  uint32_t aux;
  uint64_t rax, rdx;
  asm volatile ( "rdtscp\n" : "=a" (rax), "=d" (rdx), "=c" (aux) : : );
  return (rdx << 32) + rax;
}
#endif

// inline logging method for synchronous semantic logging
inline void Savitar_persister_log(uint64_t active_tx_id) {
    PRINT("[%d] Creating synchronous semantic log -- %zu active operations\n",
            (int)pthread_self(), active_tx_id + 1);
    uint64_t log_offset;
    PersistentObject *nv_object =
        (PersistentObject *)sync_buffer[active_tx_id].obj_ptr;
    if (active_tx_id > 0) { //dependant (nested) transaction
        uint64_t nested_tx_tag = tx_buffer[active_tx_id] | NESTED_TX_TAG;
        PersistentObject *parent =
            (PersistentObject *)sync_buffer[active_tx_id - 1].obj_ptr;
        ArgVector vector[2];
        vector[0].addr = &nested_tx_tag;
        vector[0].len = sizeof(nested_tx_tag);
        vector[1].addr = parent->getUUID();
        vector[1].len = sizeof(uuid_t);
        log_offset = nv_object->AppendLog(vector, 2);
    }
    else {
        log_offset = nv_object->Log(sync_buffer[active_tx_id].method_tag,
                sync_buffer[active_tx_id].arg_ptrs);
    }
    tx_buffer[active_tx_id + 1] = log_offset;
    sync_buffer[active_tx_id].method_tag = 0;
}

void Savitar_thread_notify(int num, ...) {
#ifdef DEBUG
    PRINT("[%d] Notifying persister with %d arguments!\n",
            (int)pthread_self(), num);
    cycles[0] = rdtscp();
#endif

    va_list valist;
    va_start(valist, num);

    uint64_t object_ptr = va_arg(valist, uint64_t);
    uint64_t method_tag = va_arg(valist, uint64_t);

    PersistentObject *obj = (PersistentObject *)object_ptr;
    if (obj->isRecovering()) {
        RecoveryContext& context = RecoveryContext::getInstance();
        PersistentObject *me = (PersistentObject *)object_ptr;
        PersistentObject *parent = context.popParentObject();
        if (parent != NULL) {
            while (!me->isWaitingForParent(parent)) { }
        }
        context.pushParentObject(me);
        return;
    }
    assert(tx_buffer[0] < MAX_ACTIVE_TXS);

    sync_buffer[tx_buffer[0]].obj_ptr = object_ptr;
    for (int i = 2; i < num; i++) {
        sync_buffer[tx_buffer[0]].arg_ptrs[i - 2] = va_arg(valist, uint64_t);
    }
    va_end(valist);

    tx_buffer[0]++;
    tx_buffer[tx_buffer[0]] = 0;
#ifndef SYNC_SL
    asm volatile("sfence" : : : "memory");
#endif // SYNC_SL

    // Don't wait if inside a nested transaction
    if (tx_buffer[0] == 1 && obj->isWaitingForSnapshot()) {
        PRINT("[%d] Worker thread is now blocked!\n", (int)pthread_self());
        tx_buffer[0] = 0;
        pthread_mutex_t *ckptLock = NVManager::getInstance().ckptLock();
        pthread_cond_t *ckptCond = NVManager::getInstance().ckptCondition();
        pthread_mutex_lock(ckptLock);
        while (obj->isWaitingForSnapshot()) {
            pthread_cond_wait(ckptCond, ckptLock);
        }
        pthread_mutex_unlock(ckptLock);
        tx_buffer[0] = 1;
        PRINT("[%d] Worker thread is now unblocked!\n", (int)pthread_self());
    }

    sync_buffer[tx_buffer[0] - 1].method_tag = method_tag;
#ifdef SYNC_SL
    Savitar_persister_log(tx_buffer[0] - 1);
    PRINT("[%d] Finished creating synchronous semantic log\n", (int)pthread_self());
#endif // SYNC_SL

#ifdef DEBUG
    char obj_uuid_str[64];
    uuid_unparse(obj->getUUID(), obj_uuid_str);
    PRINT("[%d] Opened a new transaction on %s, total open = %zu\n",
            (int)pthread_self(), obj_uuid_str, tx_buffer[0]);
    cycles[1] = rdtscp();
#endif
}

void Savitar_thread_wait(PersistentObject *object, SavitarLog *log) {
#ifdef DEBUG
    PRINT("[%d] Waiting for persister to commit!\n", (int)pthread_self());
    cycles[2] = rdtscp();
#endif
    if (object->isRecovering()) {
        /*
         * TODO optimize nested transactions recovery by using a condition
         * Signal recovery thread of this object to keep going before the
         * parent has advanced, since we know the parent transaction will
         * commit (it is already in the log).
         */
        RecoveryContext::getInstance().popParentObject();
        return;
    }
#ifndef SYNC_SL
    while (sync_buffer[tx_buffer[0] - 1].method_tag != 0) { }
#endif // SYNC_SL
    assert(tx_buffer[0] > 0);
    Savitar_log_commit(log, tx_buffer[tx_buffer[0]--]);
#ifdef DEBUG
    cycles[3] = rdtscp();
    fprintf(stdout, "%zu,%zu,%zu,%zu\n",
            cycles[1] - cycles[0],
            cycles[2] - cycles[1],
            sync_buffer[tx_buffer[0]].arg_ptrs[1] - sync_buffer[tx_buffer[0]].arg_ptrs[0],
            cycles[3] - cycles[2]);
#endif
}

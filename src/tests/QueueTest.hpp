#ifndef QUEUETEST_HPP
#define QUEUETEST_HPP

/*
 * This is a test with fixed number of operations for queues.
 */

#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "TestConfig.hpp"
#include "RQueue.hpp"
#include <random>
#ifdef PRONTO
#include <signal.h>
#include "savitar.hpp"
#include "thread.hpp"
#include "nvm_manager.hpp"
#include "snapshot.hpp"
#include <execinfo.h>
#endif
class QueueTest : public Test{
#ifdef PRONTO
    // some necessary var and func for running pronto
    static pthread_t snapshot_thread;
    static pthread_mutex_t snapshot_lock;

    static void *snapshot_worker(void *arg) {
        Snapshot *snap = (Snapshot *)arg;
        snap->create();
        delete snap;
        return NULL;
    }

    static void signal_handler(int sig, siginfo_t *si, void *unused) {
        assert(sig == SIGSEGV || sig == SIGUSR1);
        if (sig == SIGSEGV) {
            void *addr = si->si_addr;
            if (!Snapshot::anyActiveSnapshot()) {
                void *array[10];
                size_t size;

                size = backtrace(array, 10);
                fprintf(stderr, "Segmentation fault!\n");
                backtrace_symbols_fd(array, size, STDERR_FILENO);
                exit(1);
            }
            Snapshot::getInstance()->pageFaultHandler(addr);
        }
        else { // SIGUSR1
            pthread_mutex_lock(&snapshot_lock);
            if (!Snapshot::anyActiveSnapshot()) {
                Snapshot *snap = new Snapshot(PMEM_PATH);
                pthread_create(&snapshot_thread, NULL, snapshot_worker, snap);
            }
            pthread_mutex_unlock(&snapshot_lock);
        }
    }
#endif
public:
    // const std::string YCSB_PREFIX = "/localdisk2/ycsb_traces/ycsb/";
    RQueue<std::string>* q;
    // vector<std::string>** traces;
    std::string trace_prefix;
    std::string thd_num;
    size_t val_size = TESTS_VAL_SIZE;
    int prefill = 2000;
    uint64_t total_ops;
    uint64_t* thd_ops;
    unsigned int enq;
    std::string value_buffer;
    QueueTest(uint64_t o, unsigned int e = 50){
        //wl is a or b
        // trace_prefix = YCSB_PREFIX + wl + "-";
        // q = nullptr;
        total_ops = o;
        enq = e;
        assert(enq <= 100 && "enq must <= 100!");
    }
    // ~QueueTest(){
    //     if(q)
    //         delete q;
    // }

    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        q->init_thread(gtc, ltc);
#ifdef PRONTO
        if(ltc->tid==0)
            doPrefill(gtc,0);
#endif
    }

    void init(GlobalTestConfig* gtc){
#ifdef PRONTO
        // init pronto things
        Savitar_core_init();
        NVManager::getInstance(); // recover persistent objects (blocking)

        // Register signal handler for snapshots
        pthread_mutex_init(&snapshot_lock, NULL);
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = signal_handler;
        assert(sigaction(SIGSEGV, &sa, NULL) == 0);
        assert(sigaction(SIGUSR1, &sa, NULL) == 0);
#endif

        if(gtc->checkEnv("ValueSize")){
            val_size = atoi((gtc->getEnv("ValueSize")).c_str());
            assert(val_size<=TESTS_VAL_SIZE&&"ValueSize dynamically passed in is greater than macro TESTS_VAL_SIZE!");
        }
        value_buffer.reserve(val_size);
        value_buffer.clear();
        std::mt19937_64 gen_v(7);
        for (size_t i = 0; i < val_size - 1; i++) {
            value_buffer += (char)((i % 2 == 0 ? 'A' : 'a') + (gen_v() % 26));
        }
        value_buffer += '\0';
        getRideable(gtc);
        
        thd_num = to_string(gtc->task_num);
        if(gtc->checkEnv("prefill")){
            prefill = atoi((gtc->getEnv("prefill")).c_str());
        }
#ifndef PRONTO /* if pronto, we do prefill in parInit */
        doPrefill(gtc,0);
#endif
        thd_ops = new uint64_t[gtc->task_num];
        uint64_t new_ops = total_ops/gtc->task_num;
        for(int i=0;i<gtc->task_num;i++){
            thd_ops[i] = new_ops;
        }
        if(new_ops*gtc->task_num != total_ops) {
            thd_ops[0] += (total_ops - new_ops*gtc->task_num);
        }
        
        // /* get workload */
        // trace_prefix = trace_prefix + "load-" + thd_num + ".";
        // if(gtc->verbose){
        //     cout<<"YCSB trace prefixed "<<trace_prefix<<endl;
        // }
        // traces = new vector<std::string>* [gtc->task_num];
        // for(int i=0;i<gtc->task_num;i++){
        //     traces[i] = new vector<std::string>();
        //     std::ifstream infile(trace_prefix+to_string(i));
        //     std::string cmd;
        //     while(getline(infile, cmd)){
        //         traces[i]->push_back(cmd);
        //     }
        // }

        /* set interval to inf so this won't be killed by timeout */
        gtc->interval = numeric_limits<double>::max();
    }

    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        int tid = ltc->tid;
        std::mt19937_64 gen_p(ltc->seed);
        for (size_t i = 0; i < thd_ops[ltc->tid]; i++) {
            unsigned p = gen_p()%100;
            if (p<enq) {
                q->enqueue(value_buffer, ltc->tid);
            }
            else {
                q->dequeue(tid);
            }
        }
        return thd_ops[ltc->tid];
    }
    // void operation(unsigned p, string& value_buffer, int tid){
        
    //     if (p<enq) {
    //         q->enqueue(value_buffer, tid);
    //     }
    //     else {
    //         q->dequeue(tid);
    //     }
    // }

    void cleanup(GlobalTestConfig* gtc){
#ifdef PRONTO
        // Wait for active snapshots to complete
        pthread_mutex_lock(&snapshot_lock);
        if (Snapshot::anyActiveSnapshot()) {
            pthread_join(snapshot_thread, NULL);
        }
        pthread_mutex_unlock(&snapshot_lock);

        Savitar_core_finalize();
        pthread_mutex_destroy(&snapshot_lock);
#endif
        delete q;
    }
    void getRideable(GlobalTestConfig* gtc){
        Rideable* ptr = gtc->allocRideable();
        q = dynamic_cast<RQueue<std::string>*>(ptr);
        if(!q){
            errexit("QueueTest must be run on RQueue<string> type object.");
        } 
    }
    void doPrefill(GlobalTestConfig* gtc, int tid){
        if(this->prefill > 0){
            int i = 0;
            for(i = 0; i < this->prefill; i++){
                q->enqueue(value_buffer, 0);
            }
            if(gtc->verbose){
                printf("Prefilled %d\n", i);
            }
        }
    }
};
#ifdef PRONTO
pthread_t QueueTest::snapshot_thread;
pthread_mutex_t QueueTest::snapshot_lock;
#endif
// template <>
// void QueueTest<std::string>::init(GlobalTestConfig* gtc){
//     cerr<<"QueueTest doesn't support string type!"<<std::endl;
//     exit(1);
// }
#endif
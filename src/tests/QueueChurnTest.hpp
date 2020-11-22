#ifndef QUEUECHURNTEST_HPP
#define QUEUECHURNTEST_HPP

/*
 * This is a test with a time length for queues.
 */

#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "TestConfig.hpp"
#include "RQueue.hpp"

class QueueChurnTest : public Test{
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
    using V=std::string;
    int prop_enqs, prop_deqs;
    int prefill = 2000;
    size_t val_size = TESTS_VAL_SIZE;
    std::string value_buffer; // for string kv only
    RQueue<V>* q;

    QueueChurnTest(int p_enqs, int p_deqs, int prefill){
        prop_enqs = p_enqs;
        prop_deqs = prop_enqs + p_deqs;

        this->prefill = prefill;
    }

    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        q->init_thread(gtc, ltc);
#ifdef PRONTO
        if(ltc->tid==0)
            doPrefill(gtc);
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
        
        if(gtc->verbose){
            printf("Enqueues:%d Dequeues:%d\n",
            prop_enqs,100-prop_enqs);
        }
        
        // overrides for constructor arguments
        if(gtc->checkEnv("prefill")){
            prefill = atoi((gtc->getEnv("prefill")).c_str());
        }
#ifndef PRONTO
        doPrefill(gtc);
#endif
    }

    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        auto time_up = gtc->finish;
        
        int ops = 0;
        uint64_t r = ltc->seed;
        // std::mt19937_64 gen_v(r);
        std::mt19937_64 gen_p(r);

        int tid = ltc->tid;

        // atomic_thread_fence(std::memory_order_acq_rel);
        //broker->threadInit(gtc,ltc);
        auto now = std::chrono::high_resolution_clock::now();

        while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){

            int p = abs((long)gen_p()%100);
            // int p = abs(rand_nums[(p_idx++)%1000]%100);
            
            operation(p, tid);
            
            ops++;
            if (ops % 500 == 0){
                now = std::chrono::high_resolution_clock::now();
            }
            // TODO: replace this with __rdtsc
            // or use hrtimer (high-resolution timer API in linux.)
        }
        return ops;
    }

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
        q = dynamic_cast<RQueue<V>*>(ptr);
        if(!q){
            errexit("QueueChurnTest must be run on RQueue<V> type object.");
        } 
    }
    void doPrefill(GlobalTestConfig* gtc){
        if (this->prefill > 0){
            int i = 0;
            while(i<this->prefill){
                q->enqueue(value_buffer,0);
                i++;
            }
            if(gtc->verbose){
                printf("Prefilled %d\n",i);
            }
        }
    }

    void operation(int op, int tid){
        if(op < this->prop_enqs){
            q->enqueue(value_buffer, tid);
        }
        else{// op<=prop_deqs
            q->dequeue(tid);
        }
    }
};

#ifdef PRONTO
pthread_t QueueChurnTest::snapshot_thread;
pthread_mutex_t QueueChurnTest::snapshot_lock;
#endif

#endif
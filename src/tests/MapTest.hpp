#ifndef MAPTEST_HPP
#define MAPTEST_HPP

/*
 * This is a test with fixed number of operations for mappings.
 */

#include "TestConfig.hpp"
#include "RMap.hpp"
#include <iostream>
#ifdef PRONTO
#include <signal.h>
#include "savitar.hpp"
#include "thread.hpp"
#include "nvm_manager.hpp"
#include "snapshot.hpp"
#include <execinfo.h>
#endif
//KEY_SIZE and VAL_SIZE are only for string kv
template <class K, class V>
class MapTest : public Test{
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
	int pg,pr,pp,pi,pv;
	int prop_gets, prop_puts, prop_inserts, prop_removes;
	int range;
	int prefill;

	RMap<K,V>* m;
	size_t key_size = TESTS_KEY_SIZE;
	size_t val_size = TESTS_VAL_SIZE;
	std::string value_buffer; // for string kv only
    uint64_t total_ops;
    uint64_t* thd_ops;
	MapTest(int p_gets, int p_puts, int p_inserts, int p_removes, 
      int range, int prefill = 0, int op = 10000000){
        pg = p_gets;
        pp = p_puts;
        pi = p_inserts;
        pv = p_removes;

        int sum = p_gets;
        prop_gets = sum;
        sum+=p_puts;
        prop_puts = sum;
        sum+=p_inserts;
        prop_inserts = sum;
        sum+=p_removes;
        prop_removes = sum;
        
        this->range = range;
        this->prefill = prefill;

        total_ops = op;
    }

	inline K fromInt(uint64_t v);
    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        m->init_thread(gtc, ltc);
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

        getRideable(gtc);
        
        if(gtc->verbose){
            printf("Gets:%d Puts:%d Inserts:%d Removes: %d\n",
            pg,pp,pi,pv);
        }
        
        // overrides for constructor arguments
        if(gtc->checkEnv("range")){
            range = atoi((gtc->getEnv("range")).c_str());
        }
        if(gtc->checkEnv("prefill")){
            prefill = atoi((gtc->getEnv("prefill")).c_str());
        }


		if(gtc->checkEnv("KeySize")){
            key_size = atoi((gtc->getEnv("KeySize")).c_str());
			assert(key_size<=TESTS_KEY_SIZE&&"KeySize dynamically passed in is greater than macro TESTS_KEY_SIZE!");
        }
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
#ifndef PRONTO /* if pronto, we do prefill in parInit */
        doPrefill(gtc);
#endif

        thd_ops = new uint64_t[gtc->task_num];
        uint64_t new_ops = total_ops/gtc->task_num;
        for(int i=0;i<gtc->task_num;i++){
            thd_ops[i] = new_ops;
        }
        if(new_ops*gtc->task_num != total_ops) {
            thd_ops[0] += (total_ops - new_ops*gtc->task_num);
        }
        /* set interval to inf so this won't be killed by timeout */
        gtc->interval = numeric_limits<double>::max();
	}

	void getRideable(GlobalTestConfig* gtc){
		Rideable* ptr = gtc->allocRideable();
		m = dynamic_cast<RMap<K, V>*>(ptr);
		if (!m) {
			 errexit("MapTest must be run on RMap<K,V> type object.");
		}
	}
	void doPrefill(GlobalTestConfig* gtc){
		if (this->prefill > 0){
            /* Wentao: 
			 *	to avoid repeated k during prefilling, we instead 
			 *	insert [0,min(prefill-1,range)] 
			 */
			// std::mt19937_64 gen_k(0);
			// int stride = this->range/this->prefill;
			int i = 0;
			while(i<this->prefill){
				K k = this->fromInt(i%range);
				m->insert(k,k,0);
				i++;
			}
			if(gtc->verbose){
				printf("Prefilled %d\n",i);
			}
		}
	}
    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        auto time_up = gtc->finish;
        
        uint64_t r = ltc->seed;
        std::mt19937_64 gen_k(r);
        std::mt19937_64 gen_p(r+1);

        int tid = ltc->tid;

        for (size_t i = 0; i < thd_ops[tid]; i++) {
            r = abs((long)gen_k()%range);
            int p = abs((long)gen_p()%100);
            operation(r, p, tid);
        }
        return thd_ops[tid];
    }
	void operation(uint64_t key, int op, int tid){
		K k = this->fromInt(key);
		V v = k;
		// printf("%d.\n", r);
		
		if(op<this->prop_gets){
			m->get(k,tid);
		}
		else if(op<this->prop_puts){
			m->put(k,v,tid);
		}
		else if(op<this->prop_inserts){
			m->insert(k,v,tid);
		}
		else{ // op<=prop_removes
			m->remove(k,tid);
		}
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
        delete m;
    }

};

template <class K, class V>
inline K MapTest<K,V>::fromInt(uint64_t v){
	return (K)v;
}

template<>
inline std::string MapTest<std::string,std::string>::fromInt(uint64_t v){
	auto _key = std::to_string(v);
	return "user"+std::string(key_size-_key.size()-5,'0')+_key; // 31 in total; last one left for terminating null
}

template<>
inline void MapTest<std::string,std::string>::doPrefill(GlobalTestConfig* gtc){
	// randomly prefill until specified amount of keys are successfully inserted
	if (this->prefill > 0){
		std::mt19937_64 gen_k(0);
		// int stride = this->range/this->prefill;
		int i = 0;
		while(i<this->prefill){
			std::string k = this->fromInt(gen_k()%range);
			m->insert(k,value_buffer,0);
			i++;
		}
		if(gtc->verbose){
			printf("Prefilled %d\n",i);
		}
	}
}

template<>
inline void MapTest<std::string,std::string>::operation(uint64_t key, int op, int tid){
	std::string k = this->fromInt(key);
	// printf("%d.\n", r);
	
	if(op<this->prop_gets){
		m->get(k,tid);
	}
	else if(op<this->prop_puts){
		m->put(k,value_buffer,tid);
	}
	else if(op<this->prop_inserts){
		m->insert(k,value_buffer,tid);
	}
	else{ // op<=prop_removes
		m->remove(k,tid);
	}
}

#ifdef PRONTO
template<class K, class V>
pthread_t MapTest<K,V>::snapshot_thread;
template<class K, class V>
pthread_mutex_t MapTest<K,V>::snapshot_lock;
#endif
#endif
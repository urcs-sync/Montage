#ifndef CHURNTEST_HPP
#define CHURNTEST_HPP

#include "TestConfig.hpp"
#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "persist_struct_api.hpp"

class ChurnTest : public Test{
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

	ChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range, int prefill);
	ChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range,0){}
	void init(GlobalTestConfig* gtc);
	int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
	pthread_barrier_t barrier;

	virtual void cleanup(GlobalTestConfig* gtc);
	virtual void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc);
	virtual void getRideable(GlobalTestConfig* gtc) = 0;
	virtual void doPrefill(GlobalTestConfig* gtc) = 0;
	virtual void operation(uint64_t key, int op, int tid) = 0;
};

ChurnTest::ChurnTest(int p_gets, int p_puts, 
 int p_inserts, int p_removes, int range, int prefill){
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
}

void ChurnTest::parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
#ifdef PRONTO
	if(ltc->tid==0)
		doPrefill(gtc);
#endif
}

void ChurnTest::init(GlobalTestConfig* gtc){
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

	// // init Persistent allocator
	// Persistent::init();

	// // init epoch system
	// pds::init(gtc);

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
#ifndef PRONTO
	doPrefill(gtc);
#endif
	
}

int ChurnTest::execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
	auto time_up = gtc->finish;
	
	int ops = 0;
	uint64_t r = ltc->seed;
	std::mt19937_64 gen_k(r);
	std::mt19937_64 gen_p(r+1);

	int tid = ltc->tid;

	// atomic_thread_fence(std::memory_order_acq_rel);
	//broker->threadInit(gtc,ltc);
	auto now = std::chrono::high_resolution_clock::now();

	while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){

		r = abs((long)gen_k()%range);
		// r = abs(rand_nums[(k_idx++)%1000]%range);
		int p = abs((long)gen_p()%100);
		// int p = abs(rand_nums[(p_idx++)%1000]%100);
		
		operation(r, p, tid);
		
		ops++;
		if (ops % 512 == 0){
			now = std::chrono::high_resolution_clock::now();
		}
		// TODO: replace this with __rdtsc
		// or use hrtimer (high-resolution timer API in linux.)
	}
	return ops;
}

void ChurnTest::cleanup(GlobalTestConfig* gtc){
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
	// pds::finalize();
	// Persistent::finalize();
}

#ifdef PRONTO
pthread_t ChurnTest::snapshot_thread;
pthread_mutex_t ChurnTest::snapshot_lock;
#endif
#endif
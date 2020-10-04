#include "ParallelLaunch.hpp"
#include "HarnessUtils.hpp"
#include <atomic>
#include <chrono>
#include <hwloc.h>
#ifdef PRONTO
#include "savitar.hpp"
#endif
using namespace std;

// BARRIERS --------------------------------------------

// utility barrier function using pthreads barrier
// for timing the other primitives
pthread_barrier_t pthread_barrier;
void barrier()
{
	pthread_barrier_wait(&pthread_barrier);
}
void initSynchronizationPrimitives(int task_num){
	// create barrier
	pthread_barrier_init(&pthread_barrier, NULL, task_num);
}

// ALARM handler ------------------------------------------
// in case of infinite loop
bool testComplete;
void alarmhandler(int sig){
	if(testComplete==false){
		fprintf(stderr,"Time out error.\n");
		faultHandler(sig);
	}
}


// AFFINITY ----------------------------------------------

/*
void attachThreadToCore(int core_id) {
	pthread_t my_thread = pthread_self(); 
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(core_id, &cpuset);
   
	pthread_setaffinity_np(my_thread, sizeof(cpuset), &cpuset);
}
*/

void setAffinity(GlobalTestConfig* gtc, LocalTestConfig* ltc){
	int tid = ltc->tid;
	ltc->cpuset=gtc->affinities[tid]->cpuset;
#ifndef GRAPH_RECOVERY
	hwloc_set_cpubind(gtc->topology,ltc->cpuset,HWLOC_CPUBIND_THREAD);
#endif
	ltc->cpu=gtc->affinities[tid]->os_index;
}

// TEST EXECUTION ------------------------------
// Initializes any locks or barriers we need for the tests
void initTest(GlobalTestConfig* gtc){
	mlockall(MCL_CURRENT | MCL_FUTURE);
	mallopt(M_TRIM_THRESHOLD, -1);	
  	mallopt(M_MMAP_MAX, 0);
	gtc->test->init(gtc);
	for(size_t i = 0; i<gtc->allocatedRideables.size() && gtc->getEnv("report")=="1"; i++){
		if(Reportable* r = dynamic_cast<Reportable*>(gtc->allocatedRideables[i])){
			r->introduce();
		}
	}
}

// function to call the appropriate test
int executeTest(GlobalTestConfig* gtc, LocalTestConfig* ltc){
	int ops =  gtc->test->execute(gtc,ltc);
	return ops;
}

// Cleans up test
void cleanupTest(GlobalTestConfig* gtc){
	for(size_t i = 0; i<gtc->allocatedRideables.size() && gtc->getEnv("report")=="1"; i++){
		if(Reportable* r = dynamic_cast<Reportable*>(gtc->allocatedRideables[i])){
			r->conclude();
		}
	}
	gtc->test->cleanup(gtc);
}


// THREAD MANIPULATION ---------------------------------------------------

// Thread manipulation from SOR sample code
// this is the thread main function.  All threads start here after creation
// and continue on to run the specified test
static void * thread_main (void *lp)
{
	atomic_thread_fence(std::memory_order_acq_rel);
	CombinedTestConfig* ctc = ((CombinedTestConfig *) lp);
	GlobalTestConfig* gtc = ctc->gtc;
	LocalTestConfig* ltc = ctc->ltc;
	int task_id = ltc->tid;
#ifndef PRONTO /* pronto sets affinity by its own */
	setAffinity(gtc,ltc);
#endif

	barrier(); // barrier all threads before timing parInit

	if(task_id==0){
		// WARNING: we are repurposing gtc->start here for timing parInit!
		gtc->start = chrono::high_resolution_clock::now();
	}

	barrier(); // barrier all threads before starting parInit

	gtc->test->parInit(gtc, ltc);

	barrier(); // barrier all threads at end of parInit

	if(task_id==0){
		gtc->parInit_time = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - gtc->start).count();
	}

	barrier(); // barrier all threads before setting times

	if(task_id==0){
        	gtc->start = chrono::high_resolution_clock::now();
        	gtc->finish=gtc->start;
			gtc->finish+=chrono::seconds{(uint64_t)gtc->interval};
	}



	barrier(); // barrier all threads before starting

	/* ------- WE WILL DO ALL OF THE WORK!!! ---------*/
	int ops = executeTest(gtc,ltc);

	// record standard statistics
	__sync_fetch_and_add (&gtc->total_operations, ops);
	gtc->recorder->reportThreadInfo("ops",ops,ltc->tid);
	gtc->recorder->reportThreadInfo("ops_stddev",ops,ltc->tid);
	gtc->recorder->reportThreadInfo("ops_each",ops,ltc->tid);

	barrier(); // barrier all threads at end
	if(task_id==0){
		auto now = chrono::high_resolution_clock::now();
		// update interval in case it's a test with undertermined length
		gtc->interval = chrono::duration_cast<chrono::microseconds>(now - gtc->start).count()/1000000.0;
		if(gtc->interval <= 0.000001) {
			gtc->interval = 0.000001;
		}
	}
	return NULL;
}


// This function creates our threads and sets them loose
void parallelWork(GlobalTestConfig* gtc){

	pthread_attr_t attr;
	pthread_t *threads;
	CombinedTestConfig* ctcs;
	int i;
	int task_num = gtc->task_num;

	// init globals
	initSynchronizationPrimitives(task_num);
	initTest(gtc);
	testComplete = false;

	// initialize threads and arguments ----------------
	ctcs = (CombinedTestConfig *) malloc (sizeof (CombinedTestConfig) * gtc->task_num);
	threads = (pthread_t *) malloc (sizeof (pthread_t) * gtc->task_num);
	if (!ctcs || !threads){ errexit ("out of shared memory"); }
	pthread_attr_init (&attr);
	pthread_attr_setscope (&attr, PTHREAD_SCOPE_SYSTEM);
	//pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 1024*1024);
	srand((unsigned) time(NULL));
	for (i = 0; i < task_num; i++) {
		ctcs[i].gtc = gtc;
		ctcs[i].ltc = new LocalTestConfig();
		ctcs[i].ltc->tid=i;
		ctcs[i].ltc->seed = rand();
	}

	signal(SIGALRM, &alarmhandler);  // set a signal handler
	if(gtc->timeOut){
		/* Wentao: 
		 * Waiting for 10 seconds somehow isn't enough for Mnemosyne and Pronto
		 * so we extend it to 30. This however is just a workaround. 
		 * TODO will fix it.
		 */

		alarm(gtc->interval+30);  // set an alarm for interval+30 seconds from now
	}

	// atomic_thread_fence(std::memory_order_acq_rel);

	// launch threads -------------
#ifdef PRONTO
	/* Spawn worker threads
	 * Note: Due to complex of pronto worker init, we choose not to reuse this 
	 * main thread as a worker.
	 */
	for (i = 0; i < task_num; i++) {
		Savitar_thread_create(&threads[i], &attr, thread_main, &ctcs[i]);
	}


	// All threads working here... ( in thread_main() )
	

	// join threads ------------------
	for (i = 0; i < task_num; i++)
    	pthread_join (threads[i], NULL);
#else
	// Spawn worker threads and reuse this main thread as worker, too
	for (i = 1; i < task_num; i++) {
		pthread_create (&threads[i], &attr, thread_main, &ctcs[i]);
	}
	//pthread_key_create(&thread_id_ptr, NULL);
	thread_main(&ctcs[0]); // start working also


	// All threads working here... ( in thread_main() )
	

	// join threads ------------------
	for (i = 1; i < task_num; i++)
    	pthread_join (threads[i], NULL);
#endif

	for (i = 0; i < task_num; i++) {
		delete ctcs[i].ltc;
	}


	testComplete = true;
	free(ctcs);
	free(threads);
	cleanupTest(gtc);
}

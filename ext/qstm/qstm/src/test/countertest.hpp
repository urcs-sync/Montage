#ifndef COUNTERTEST_HPP
#define COUNTERTEST_HPP

#include <pthread.h>
#include <iostream>
#include "transaction.hpp"
#include "testconfig.hpp"
#include "testlaunch.hpp"

#include <setjmp.h>

// #define ITER 10000000

class CounterTest : public TransTest{
	int thread_cnt;
	
		
public:
	static unsigned long counter; // Global counter
	static QSTMFactory* stm_factory;
	static TestConfig* tc;
	static long iter;
	// Spawns threads, joins threads, displays counter
	CounterTest(TestConfig* c){
		tc = c;
		stm_factory = tc->getTMFactory();
		thread_cnt = tc->thread_cnt;
		counter = 0;
		iter = 1000;
		if (tc->checkEnv("iter")){
			iter = std::stoi(tc->getEnv("iter"));
		}
	}
	// Worker increments counter in transaction, with manual retries
	static void *workerCount(void *threadid) {
		long tid;
		tid = (long)threadid;

		setAffinity(tc, tid);

		unsigned long val;
		// transaction txn;
		auto* txn = stm_factory->build(tc, tid);

		barrier();

		for(int i=0; i<iter; i++){
			setjmp(txn->env);
			txn->tm_begin();
			val = (unsigned long)txn->tm_read((void**)&counter);
			val++;
			txn->tm_write((void**)&counter, (void*)val);
			txn->tm_end();
		}

		delete(txn);
		pthread_exit(NULL);
	}
	void run(){
		int rc;
		long i;
		pthread_t threads[thread_cnt];
		pthread_attr_t attr;
		void *status;

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		std::cout << "Spawning threads\n";
		for(i = 0; i < thread_cnt; i++) {
			rc = pthread_create(&threads[i], NULL, workerCount, (void *)i );

			if (rc) {
				exit(-1);
			}
		}

		pthread_attr_destroy(&attr);
		std::cout << "All threads spawned successfully\n";
		for(i = 0; i < thread_cnt; i++){
			rc = pthread_join(threads[i], &status);
			if(rc) exit(-1);
		}

		// free (status);

		// std::cout << "Counter val: " << counter << std::endl;
		// pthread_exit(NULL);
	}

	void finalize(){
		std::cout << "Counter val: " << counter << std::endl;
		// pthread_exit(NULL);
	}
};
#if defined(RINGSTM)
RingSTMFactory* CounterTest::stm_factory;
#elif defined(OLDQUEUESTM)
OLDQSTMFactory* CounterTest::stm_factory;
#elif defined(QUEUESTM)
QSTMFactory* CounterTest::stm_factory;
#endif
TestConfig* CounterTest::tc;
unsigned long CounterTest::counter;
long CounterTest::iter = 10000000;

class CounterTestFactory : public TransTestFactory{
	TransTest* build(TestConfig* tc){
		return new CounterTest(tc);
	}
};



#endif

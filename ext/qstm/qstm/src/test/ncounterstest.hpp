#ifndef NCOUNTERSTEST_HPP
#define NCOUNTERSTEST_HPP

#include <pthread.h>
#include <iostream>
#include "transaction.hpp"
#include "testconfig.hpp"
#include "concurrentprimitives.hpp"
#include "testlaunch.hpp"

#include <setjmp.h>

// #define ITER 10000000

class NCountersTest : public TransTest{
	int thread_cnt;
	
		
public:
	// static unsigned long counter; // Global counter
	static padded<unsigned long>* counters; // Global counters
	static QSTMFactory* stm_factory;
	static TestConfig* tc;
	static long iter;
	static int amount;
	// Spawns threads, joins threads, displays counter
	NCountersTest(TestConfig* c){
		tc = c;
		stm_factory = tc->getTMFactory();
		iter = 1000;
		amount = 1000;
		if (tc->checkEnv("iter")){
			iter = std::stoi(tc->getEnv("iter"));
		}
		if (tc->checkEnv("N")){
			amount = std::stoi(tc->getEnv("N"));
		}

		thread_cnt = tc->thread_cnt;
		// counter = 0;
		counters = new padded<unsigned long>[amount];
		for (int i = 0; i < amount; i++){
			counters[i].ui = 0;
		}
	}
	// Worker increments counter in transaction, with manual retries
	static void *workerCount(void *threadid) {
		long tid;
		tid = (long)threadid;
		RP_set_tid(tid);
		unsigned long val;

		setAffinity(tc, tid);

		// padded<unsigned long>* vals = new padded<unsigned long>[3];
		// transaction txn;
		auto* txn = stm_factory->build(tc, tid);

		barrier();

		for(int i=0; i<iter; i++){
			setjmp(txn->env);
			txn->tm_begin();
			for (int j=0; j<amount; j++){
				val = (unsigned long)txn->tm_read((void**)&counters[j].ui);
				val++;
				txn->tm_write((void**)&counters[j].ui, (void*)val);
			}
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
		// pthread_exit(NULL);
	}

	void finalize(){
		// std::cout << "Counter val: " << counter << std::endl;
		for (int i = 0; i < amount; i++){
			std::cout << "Counter"<<i<<" val: " << counters[i].ui << std::endl;
		}
		// pthread_exit(NULL);
	}
};

#if defined(RINGSTM)
RingSTMFactory* NCountersTest::stm_factory;
#elif defined(OLDQUEUESTM)
OLDQSTMFactory* NCountersTest::stm_factory;
#elif defined(QUEUESTM)
QSTMFactory* NCountersTest::stm_factory;
#endif
TestConfig* NCountersTest::tc;
padded<unsigned long>* NCountersTest::counters;
long NCountersTest::iter = 10000000;
int NCountersTest::amount = 3;

class NCountersTestFactory : public TransTestFactory{
	TransTest* build(TestConfig* tc){
		return new NCountersTest(tc);
	}
};



#endif

#include <pthread.h>
#include <iostream>
//#include "../ringstm.hpp"
#include "../qstm.hpp"

#define NUM_THREADS 1
#define ITER 10000000

unsigned long counter = 0; // Global counter

// Worker increments counter in transaction, with manual retries
void *workerCount(void *threadid) {
	long tid;
	tid = (long)threadid;
	unsigned long val;
	transaction txn;

	for(int i=0; i<ITER; i++){
		do{
			txn.tm_begin();
			val = (unsigned long)txn.tm_read((void**)&counter);
			val++;
			txn.tm_write((void**)&counter, (void*)val);
		}while(!txn.tm_end());
	}
	pthread_exit(NULL);
}

// Spawns threads, joins threads, displays counter
int main(){
	int rc;
	long i;
	pthread_t threads[NUM_THREADS];
	pthread_attr_t attr;
	void *status;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	std::cout << "Spawning threads\n";
	for(i = 0; i < NUM_THREADS; i++) {
		rc = pthread_create(&threads[i], NULL, workerCount, (void *)i );

		if (rc) {
			exit(-1);
		}
	}

	pthread_attr_destroy(&attr);
	std::cout << "All threads spawned successfully\n";
	for(i = 0; i < NUM_THREADS; i++){
		rc = pthread_join(threads[i], &status);
		if(rc) exit(-1);
	}

	std::cout << "Counter val: " << counter << std::endl;
	pthread_exit(NULL);
}

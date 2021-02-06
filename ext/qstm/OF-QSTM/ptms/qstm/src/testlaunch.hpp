#ifndef TESTLAUNCH_HPP
#define TESTLAUNCH_HPP

#include "transaction.hpp"
#include "ringstm.hpp"
#include "qstm-old.hpp"
#include "qstm.hpp"
#if defined(RINGSTM)
class RingSTM;
#elif defined(OLDQUEUESTM)
class OLDQSTM;
#elif defined(QUEUESTM)
class QSTM;
#endif
class TransTest;

extern pthread_barrier_t pthread_barrier;
// void barrier()
// {
// 	pthread_barrier_wait(&pthread_barrier);
// }
// void initSynchronizationPrimitives(int task_num){
// 	// create barrier
// 	pthread_barrier_init(&pthread_barrier, NULL, task_num);
// }

// void setAffinity(TestConfig* tc, int tid){
// 	hwloc_cpuset_t cpuset = tc->affinities[tid]->cpuset;
// 	hwloc_set_cpubind(tc->topology,cpuset,HWLOC_CPUBIND_THREAD);
// }

// void testLaunch(TestConfig* tc){
// 	TransTest* test = tc->tests[tc->test]->build(tc); //build test object
// 	tc->stms[tc->stm]->init(tc); //init STM system
// 	initSynchronizationPrimitives(tc->thread_cnt);
// 	test->run(); //run the test
// 	test->finalize();
// 	delete(test);
// }

void barrier();
void initSynchronizationPrimitives(int task_num);

void setAffinity(TestConfig* tc, int tid);

void testLaunch(TestConfig* tc);

#endif

#include "testlaunch.hpp"

pthread_barrier_t pthread_barrier;

void barrier()
{
	pthread_barrier_wait(&pthread_barrier);
}
void initSynchronizationPrimitives(int task_num){
	pthread_barrier_init(&pthread_barrier, NULL, task_num);
}

void setAffinity(TestConfig* tc, int tid){
	hwloc_cpuset_t cpuset = tc->affinities[tid]->cpuset;
	hwloc_set_cpubind(tc->topology,cpuset,HWLOC_CPUBIND_THREAD);
}

void testLaunch(TestConfig* tc){
	TransTest* test = tc->tests[tc->test]->build(tc); //build test object
	tc->stm->init(tc); //init STM system
	initSynchronizationPrimitives(tc->thread_cnt);
	test->run(); //run the test
	test->finalize();
	delete(test);
}

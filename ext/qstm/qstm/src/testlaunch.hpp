#ifndef TESTLAUNCH_HPP
#define TESTLAUNCH_HPP

#include "transaction.hpp"
#include "qstm.hpp"
class QSTM;
class TransTest;

extern pthread_barrier_t pthread_barrier;

void barrier();
void initSynchronizationPrimitives(int task_num);

void setAffinity(TestConfig* tc, int tid);

void testLaunch(TestConfig* tc);

#endif

#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <hwloc.h>

#include "testconfig.hpp"
#include "concurrentprimitives.hpp"
#include "tracker.hpp"

#include <setjmp.h>

class TestConfig;
class Tracker;

class TM{
public:
	int tid;
	static void init(TestConfig* tc) {}
	jmp_buf env;
	unsigned long nesting = 0;

	TM(int id): tid(id){}
	virtual ~TM(){}
};

class TMFactory{
public:
	virtual void init(TestConfig* tc) = 0;
	virtual TM* build(TestConfig* tc, int tid) = 0;
	virtual ~TMFactory(){}
};

class TransTest{
public:
	virtual void run() = 0;
	virtual void finalize() = 0;
	virtual ~TransTest(){}
};

class TransTestFactory{
public:
	virtual TransTest* build(TestConfig* tc) = 0;
	virtual ~TransTestFactory(){}
};

#endif

#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <hwloc.h>

#include "testconfig.hpp"
#include "concurrentprimitives.hpp"
#include "trackers/tracker.hpp"

#include <setjmp.h>

class TestConfig;
class Tracker;

class TM{
public:
	int tid;
	/*virtual void tm_begin() = 0;
	virtual void *tm_read(void** addr) = 0;
	virtual void tm_write(void** addr, void* val, void* mask = 0) = 0;
	virtual bool tm_end() = 0;
	virtual void tm_clear() = 0;
	virtual void* tm_malloc(size_t size) = 0;
	virtual void tm_free(void **ptr) = 0;
*/
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

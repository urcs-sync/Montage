#ifndef RINGSTM_H
#define RINGSTM_H

#include "transaction.hpp"
#include "concurrentprimitives.hpp"

#include "util/writeset.hpp"
#include "util/mallocset.hpp"
#include "util/freeset.hpp"
#include "util/ringbuf.hpp"
#include "util/bloomfilter.hpp"

// The global ring, visible to all threads
extern ringbuf ring; //TODO: put this into ringstm class.
class TestConfig;

class RingSTM : public TM{
public:
	void tm_begin();
	void *tm_read(void** addr);
	void tm_write(void **addr, void *val, void *mask = 0);
	bool tm_end();
	void tm_clear();
	void tm_abort();

	void* tm_malloc(size_t size);
	void tm_free(void **ptr);

	static void init(TestConfig* tc);

	//bool abort = false;

	RingSTM(TestConfig* tc, int tid);

private:
	void check();

	writeset* wset;
	mallocset* mset;
	freeset* fset;
	// static padded<writeset*>* wset;
	filter wf;
	filter rf;
	unsigned long start = 0;
};

// padded<writeset*>* RingSTM::wset;

class RingSTMFactory{
public:
	void init(TestConfig* tc){
		RingSTM::init(tc);
	}
	RingSTM* build(TestConfig* tc, int tid){
		return new RingSTM(tc, tid);
	}
};

#endif

#ifndef QSTMOLD_H
#define QSTMOLD_H

#include "transaction.hpp"

#include "util/writeset.hpp"
#include "util/mallocset.hpp"
#include "util/freeset.hpp"
#include "util/bloomfilter.hpp"
#include "util/entry.hpp"
#include "tracker.hpp"
#include <atomic>


// The global queue, visible to all threads
// extern queue_t queue; //TODO: put this into OLDQSTM class.


class OLDQSTM: public TM{
public:
	class writesetQSTM : public writeset{
	public:
		~writesetQSTM();
	};
	class qEntry : public Entry, public Trackable{
	public:
		qEntry();
		writeset* wset_p;
		freeset* fset_p;
		std::atomic<qEntry*> next;
		uint64_t get_ts();

		~qEntry();
	};
	class queue_t{
	public:
		queue_t();
		void enqueue(qEntry *foo);
		qEntry * dequeue();
		qEntry * dequeue_until(unsigned long until_ts, unsigned long& cnt_ui);
		std::atomic<qEntry*> head;
		std::atomic<qEntry*> complete;
		std::atomic<qEntry*> tail;
		std::atomic<bool> writelock;
	};

	void tm_begin();
	void *tm_read(void** addr);
	void tm_write(void** addr, void* val, void *mask=nullptr);
	bool tm_end();
	void tm_clear();
	void tm_abort();
	void do_writes_htm(qEntry *q);

	void* tm_malloc(size_t size);
	void tm_free(void **ptr);

	static void init(TestConfig* tc);
	static Tracker* tracker;
	static queue_t* queue;

	OLDQSTM(TestConfig* tc, int tid);
	~OLDQSTM();

private:
	void check();
	void queue_worker();

	writesetQSTM* wset;
	mallocset* mset;
	freeset* fset;

	bool wset_published = false;
	filter wf;
	filter rf;
	qEntry *start = nullptr;
};

class OLDQSTMFactory{
public:
	void init(TestConfig* tc){
		OLDQSTM::init(tc);
	}
	OLDQSTM* build(TestConfig* tc, int tid){
		return new OLDQSTM(tc, tid);
	}
};

#endif

#ifndef QSTM_H
#define QSTM_H

#include "transaction.hpp"
#include "concurrentprimitives.hpp"
#include "../config.hpp"

#include "util/writeset.hpp"
#include "util/mallocset.hpp"
#include "util/freeset.hpp"
#include "util/bloomfilter.hpp"
#include "util/entry.hpp"
#include "trackers/tracker.hpp"

#include <atomic>

class QSTM: public TM{
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
		// static std::atomic<uint64_t> cnt;
		void detach();
		void init();
		void *operator new(size_t sz){
			return pstm_pmalloc(sz);
		}
		void *operator new[](size_t sz){
			return pstm_pmalloc(sz);
		}
		void operator delete(void *p){
			pstm_pfree(p);
		}
		void operator delete[](void *p){
			pstm_pfree(p);
		}
		~qEntry();
	};
	class queue_t{
	public:
		queue_t();
		void enqueue(qEntry *foo);
		qEntry * dequeue_one_until(unsigned long until_ts);
		qEntry * dequeue_until(unsigned long until_ts, unsigned long& cnt_ui);
		std::atomic<qEntry*> head;
		std::atomic<qEntry*> complete;
		std::atomic<qEntry*> tail;
		std::atomic<bool> writelock;
std::atomic<uint64_t> txncount;
	};
	void tm_begin();
	void *tm_read(void** addr);
	void tm_write(void** addr, void* val, void *mask=nullptr);
	bool tm_end();
	void tm_clear();
	void tm_abort();
	int do_writes_qstm(qEntry *q);

	void* tm_pmalloc(size_t size);
	void tm_pfree(void **ptr);
	void* tm_malloc(size_t size);
	void tm_free(void **ptr);

//	static void init(TestConfig* tc);
	static void init();
	// static Tracker* tracker;
	static queue_t* queue;

	// GC stuff
	static std::atomic<uint64_t> complete_snapshot;
	static std::atomic<uint64_t> min_reserv_snapshot;
	static paddedAtomic<uint64_t>* reservs;
	static padded<std::list<qEntry*>>* retired;
	static padded<uint64_t>* retire_counters;
	static bool collect;
	static int empty_freq;
	static int thread_cnt;
	// void retire(qEntry* obj, int tid);
	void reserve(int tid);
	uint64_t get_min_reserve();
	void release(int tid);
	// void empty(int tid);

	QSTM(TestConfig* tc, int tid);
	~QSTM();

private:
	void check();
	void gc_worker();
	void wb_worker();
	qEntry* get_new_qEntry();

	qEntry* my_txn;
	writesetQSTM* wset;
	mallocset* mset;
	freeset* fset;

	// bool wset_published = false;
	filter wf;
	filter rf;
	qEntry *start = nullptr;
};

class QSTMFactory{
public:
	void init(TestConfig* tc){
		QSTM::init();
	}
	QSTM* build(TestConfig* tc, int tid){
		return new QSTM(tc, tid);
	}
};

// Tracker* QSTM::tracker = nullptr;

#endif

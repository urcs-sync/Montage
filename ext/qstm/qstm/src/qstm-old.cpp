#include "qstm-old.hpp"
#include "util/writeset.hpp"
#include "util/mallocset.hpp"
#include "util/freeset.hpp"
#include "../config.hpp"
#include "tracker.hpp"
#include <immintrin.h> // RTM support (TSX)
#include <cstdlib>
#include <setjmp.h>

#define likely(x) __builtin_expect ((x), 1)
#define unlikely(x) __builtin_expect ((x), 0)

// The global queue, visible to all threads


OLDQSTM::qEntry::qEntry(){
	ts = 0;
	st = State::Writing;
	next.store(nullptr);
}

uint64_t OLDQSTM::qEntry::get_ts(){
	return ts;
}

OLDQSTM::qEntry::~qEntry(){
	delete(wset_p);
	fset_p->do_frees();
	delete(fset_p);
}

OLDQSTM::queue_t::queue_t(){
	head = new qEntry();
	head.load()->st = State::Complete; // Needed so that dummy entry doesn't trigger write attempts
	head.load()->wset_p = new writesetQSTM(); // Needed so that free of dummy doesn't segfault
	head.load()->fset_p = new freeset(); // Needed so that free of dummy doesn't segfault
	tail = head.load();
	complete = head.load();

#ifdef DUR_LIN
	FLUSH(&head);
	FLUSH(&complete);
	FLUSH(&tail);
	FLUSH(&head); // Flush dummy entry
	FLUSH(&head.load()->next); // Flush dummy entry
	head.load()->wset_p->flush(); // Flush dummy entry
	head.load()->fset_p->flush(); // Flush dummy entry
	FLUSHFENCE;
#endif

}

// Tries to add a queue qEntry until it succeeds
void OLDQSTM::queue_t::enqueue(qEntry *foo){
	bool success = false;

	do{
		qEntry *oldtail = tail.load();
		qEntry *oldlast = oldtail->next.load();
		if(oldlast == nullptr){
			success = tail.load()->next.compare_exchange_strong(oldlast, foo, std::memory_order_seq_cst);
			if(success){ // Fix the tail
				tail.compare_exchange_strong(oldtail, foo, std::memory_order_seq_cst);
			}
		}else{ // Tail was not fixed by earlier operation
			tail.compare_exchange_strong(oldtail, oldlast, std::memory_order_seq_cst);
		}
	}while(!success);
	return;
}

// Dequeues an qEntry and returns a pointer to it
OLDQSTM::qEntry * OLDQSTM::queue_t::dequeue(){
	bool success = false;
	qEntry *mine;
	qEntry *dummy;
	do{
		mine = head.load(); // Never nullptr, if no bugs
		dummy = mine->next.load(); // nullptr if queue is empty
		if(dummy !=nullptr){
			success = head.compare_exchange_strong(mine, dummy, std::memory_order_seq_cst);
		}else return nullptr;
	}while(!success);
	return mine;
}

// Dequeues a batch of cnt_ui qEntries, or (x<cnt_ui entries until until_ts, and set_cnt_ui
// to the actual number of entries dequeued).
// If cnt_ui=0, then the restriction will just be until_ts.
OLDQSTM::qEntry* OLDQSTM::queue_t::dequeue_until(unsigned long until_ts, unsigned long& cnt_ui){
	bool success = false;
	qEntry *oldhead;
	qEntry *dummy, *new_dummy;
	unsigned long cnt;

	do{	
		cnt = (cnt_ui? cnt_ui : ULONG_MAX);
		dummy = oldhead = head.load();
		for (unsigned long i = 0; i < cnt; i++){
			new_dummy = dummy->next.load();
			if (new_dummy == nullptr || new_dummy->ts >= until_ts){//we're keeping the last completed node here.
				cnt = i;
				break;
			}
			dummy = new_dummy;
		}
		success = head.compare_exchange_strong(oldhead, dummy, std::memory_order_seq_cst);
	}while(!success);

	cnt_ui = cnt;
	return oldhead;
}


OLDQSTM::writesetQSTM::~writesetQSTM(){
	delete [] writes;
}

void OLDQSTM::do_writes_htm(qEntry *q){
	unsigned int status;
	unsigned int retry = 0;
	while(true){
		if ((status = _xbegin ()) == _XBEGIN_STARTED) {
			if ((q->st == State::Complete) || queue->writelock.load(std::memory_order_acquire) == true){
				_xabort(1);
				return;
			}
			q->wset_p->do_writes();
			q->st = State::Complete;
			_xend ();
			// Complete state indicates that writes are persistent and do not need to be redone
#ifdef DUR_LIN
			FLUSH(&(q->st));
			FLUSHFENCE;
#endif
			return;
		} else {
			if (retry++ < DOWRITE_MAX){
				continue;
			} else {
				bool fls = false;
				while (!queue->writelock.compare_exchange_strong(fls, true, std::memory_order_seq_cst)){
					fls = false;
				}
				if(q->st != State::Complete){
					q->wset_p->do_writes();
					q->st = State::Complete;
					// Complete state indicates that writes are persistent and do not need to be redone
#ifdef DUR_LIN
					FLUSH(&(q->st));
					FLUSHFENCE;
#endif
				}
				queue->writelock.store(false, std::memory_order_release);
				return;
			}
		}
	}
}

void OLDQSTM::init(TestConfig* tc){
	queue = new queue_t();
	queue->writelock.store(false, std::memory_order_release);
	tracker = new Tracker(tc);
}

OLDQSTM::OLDQSTM(TestConfig* tc, int tid) : TM(tid){
	wset = new writesetQSTM();
	mset = new mallocset();
	fset = new freeset();
}

OLDQSTM::~OLDQSTM(){
	if (!wset_published && wset){
		delete(wset);
	}
}

void OLDQSTM::tm_begin(){
	nesting++; // A value of 1 indicates an ongoing transaction
	if(nesting > 1) return;

	if (!wset){
		wset = new writesetQSTM();
		fset = new freeset();
		wset_published = false;
	}
	start = queue->complete.load();
	qEntry *oldcomplete = start;
	qEntry *oldtail = queue->tail.load(); // To bound the traversal

	tracker->reserve(start->ts, oldtail->ts, tid); // reserve all entries from start to oldtail

	// This is CRITICAL because otherwise it is possible to block for one stuck thread if the complete ptr is too old and nobody is succeeding
	// However we must bound the traversal to prevent a treadmill problem
	while(start->ts < oldtail->ts){
		if(start->st != State::Complete){
			do_writes_htm(start);
		}
		start = start->next;
	}
	if(start->st != State::Complete){
		do_writes_htm(start);
	}

	tracker->reserve_low(start->ts, tid); // update reservation to new start.

	// Now, if the complete ptr is out of date, we fix it
	if(oldcomplete->ts < start->ts){
		queue->complete.compare_exchange_strong(oldcomplete, start, std::memory_order_seq_cst);
	}

	// we cannot release reservations here.
	return;
}

void* OLDQSTM::tm_read(void** addr){
	assert(nesting != 0);
	if(wf.contains(addr)){
		void* look;
		if (wset->lookup((void**) addr, &look))
			return look;
	}

	void *val = *addr;
	rf.add(addr);
	asm volatile ("lfence" ::: "memory");
	check();
	return val;
}

void OLDQSTM::tm_write(void** addr, void* val, void* mask){
	assert(nesting != 0);
	wset->add(addr, val, mask);
	wf.add(addr);
	return;
}

// Attempts to end a transaction, and clears all transaction data
bool OLDQSTM::tm_end(){
	assert(nesting != 0); // Shouldn't ever call this function when nesting == 0
	nesting--; // A value of 0 indicates that the outermost transaction has ended
	if(nesting > 0) return true;

	// Read-only case
	if(wset->empty()){
		wf.clear();
		rf.clear();
		return true;
	}

	// Allocate a queue entry and populate it with the right info
	qEntry *my_txn = new qEntry; // State and next are already initialized
	my_txn->wset_p = wset;
	my_txn->fset_p = fset;
	wset_published = true;
	my_txn->wf = wf;

	qEntry *oldtail; // To fix the tail later
	qEntry *prev;

#ifdef DUR_LIN
	// Note: We only need to flush the {w,f}set, {w,f}set pointer, next pointer, and ts for recovery. State is flushed in do_writes_htm()
	my_txn->wset_p->flush();
	my_txn->fset_p->flush();
	FLUSH(&my_txn->wset_p);
	FLUSH(&my_txn->fset_p);
	FLUSH(&my_txn->next);
	FLUSH(&my_txn->ts);
	FLUSHFENCE;
#endif

	// We don't ever need to persist the tail!
	bool success = false;
	qEntry *foo; // For the CAS comparison
	do{
		foo = nullptr;
		oldtail = queue->tail.load();
		if(oldtail->next.load() == nullptr){
			check();

			my_txn->ts = ((oldtail->ts)+1);
#ifdef DUR_LIN
			FLUSH(&(my_txn->ts));
			FLUSHFENCE;
#endif
			//extend reservation to my_txn
			tracker->reserve_up(my_txn->ts, tid);

			success = oldtail->next.compare_exchange_strong(foo, my_txn, std::memory_order_seq_cst);
		}else{
#ifdef DUR_LIN
			FLUSH(&(oldtail->next)); // Someone else might not have flushed this yet since the tail isn't fixed
			FLUSHFENCE;
#endif
			queue->tail.compare_exchange_strong(oldtail, oldtail->next.load(), std::memory_order_seq_cst); // Fix tail
		}
	}while(!success);

#ifdef DUR_LIN
	FLUSH(&(oldtail->next)); // We did it, so now we need to persist the CAS
	FLUSHFENCE;
#endif
	queue->tail.compare_exchange_strong(oldtail, my_txn, std::memory_order_seq_cst); // Fix tail


	// Traverse forwards from complete pointer, doing writes as you go until reaching my_txn
	// Ideally each thread does its own writes but sometimes one will get delayed
	qEntry *traverse = queue->complete.load();

	// reserve from complete to your own txn!
	tracker->reserve_low(traverse->ts, tid);

	// TODO Do we want to skip entries that we don't intersect with until we want to set the complete state?
	while(traverse->ts < my_txn->ts){
		if(traverse->st != State::Complete){
			do_writes_htm(traverse);
		}
		traverse = traverse->next.load();
	}

	// Finally, do own writes
	if(my_txn->st != State::Complete){
		do_writes_htm(traverse);
	}

	// Try to update the complete pointer
	qEntry *oldcomplete = queue->complete.load();
	while(oldcomplete->ts < my_txn->ts){
		queue->complete.compare_exchange_strong(oldcomplete, my_txn, std::memory_order_seq_cst);
		oldcomplete = queue->complete.load();
	}

	// release all reservations here
	tracker->release(tid);

	// Periodic worker tasks
	if((my_txn->ts >= 50000) &&
		((my_txn->ts - queue->head.load()->ts)%50000 == (((unsigned int)rand()) % 500))) queue_worker(); // avoid multiple workers wake up together.

	// Since our wset and fset are globally accessible, we must leave them for a worker to clean up.
	wset = nullptr;
	fset = nullptr;
	mset->clear(); // We only needed this for aborts
	wset_published = false;
	wf.clear();
	rf.clear();
	return true;
}

// Clears all transaction data without doing any writes
void OLDQSTM::tm_clear(){
	wf.clear();
	rf.clear();
	wset->clear();
	fset->clear();
	mset->undo_mallocs();
	tracker->release(tid);
	return;
}

// Jumps back to outermost tm_begin() call after resetting things as needed
void OLDQSTM::tm_abort(){
	tm_clear();
	nesting = 0;
	longjmp(env,1);
}

void* OLDQSTM::tm_malloc(size_t size){
	assert(nesting != 0);
	void *ptr = pstm_pmalloc(size);
	mset->add((void**)ptr);
	return ptr;
}

void OLDQSTM::tm_free(void **ptr){
	assert(nesting != 0);
	fset->add(ptr);
}

// Sets the transaction abort flag if there is a conflict, and otherwise adjusts start time as needed
void OLDQSTM::check(){
	unsigned long tailstamp = queue->tail.load()->ts;
	unsigned long prevts;
	if(tailstamp == start->ts) return;

	//Reserve from start up to tail
	tracker->reserve_up(tailstamp, tid);

	qEntry *suffix_end = start;
	qEntry *traverse = start->next;

	// Detect long context switch
	if(traverse == nullptr) tm_abort();

	while((traverse != nullptr) && (traverse->ts <= tailstamp)){
		if(rf.intersect(traverse->wf)){
			tm_abort();
			return;
		}
		if(traverse->st == State::Complete) suffix_end = traverse;

		prevts = traverse->ts;
		traverse = traverse->next.load();
		// Detect long context switch
		if((traverse == nullptr) && (prevts < tailstamp)) tm_abort();
	}

	start = suffix_end;

	// update lower reservation, releasing some entries
	tracker->reserve_low(suffix_end->ts, tid);

	return;
}

// Performs memory management tasks
void OLDQSTM::queue_worker(){
	qEntry *foo;
	unsigned long finish = queue->complete.load(std::memory_order_acquire)->ts;

	tracker->reserve(queue->head.load(std::memory_order_acquire)->ts, finish, tid);
	
	unsigned long batch_size;
	while(true){
		batch_size = 500;
		foo = queue->dequeue_until(finish, batch_size);
		for (unsigned long i = 0; i < batch_size; i++){
			assert(foo->ts < finish);
			tracker->retire(foo, tid);
			foo = foo->next.load();
		}
		if(batch_size < 500){
			return;
		}
	}
	return;
}

Tracker* OLDQSTM::tracker = nullptr;
OLDQSTM::queue_t* OLDQSTM::queue = nullptr;

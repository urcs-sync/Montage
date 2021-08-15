#include "qstm.hpp"
#include "util/writeset.hpp"
#include "util/mallocset.hpp"
#include "util/freeset.hpp"
#include "../config.hpp"
#include <cstdlib>
#include <setjmp.h>

#define INTERVAL_MASK 0xfffffffffffff000

char* base_addr = NULL;

QSTM::qEntry::qEntry(){
	ts = 0;
	st = State::NotWriting;
	next.store(nullptr);
}

uint64_t QSTM::qEntry::get_ts(){
	return ts;
}

void QSTM::qEntry::init(){
	ts = 0;
	st = State::NotWriting;
	next.store(nullptr);
	if (wset_p != nullptr){
		delete(wset_p);
	}
	if (fset_p != nullptr){
		fset_p->do_frees();
		delete(fset_p);
	}
}

void QSTM::qEntry::detach(){
	wset_p = nullptr;
	fset_p = nullptr;
}

QSTM::qEntry::~qEntry(){
	if (wset_p != nullptr){
		delete(wset_p);
	}
	if (fset_p != nullptr){
		fset_p->do_frees();
		delete(fset_p);
	}
	// cnt.fetch_add(-1, std::memory_order_relaxed);
}

QSTM::queue_t::queue_t(){
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

QSTM::qEntry* QSTM::queue_t::dequeue_one_until(unsigned long until_ts){
	bool success = false;
	qEntry* oldhead;
	qEntry *dummy, *new_dummy;
	do {
		dummy = head.load();
		new_dummy = dummy->next.load();
		if (new_dummy == nullptr || new_dummy->ts >= until_ts){
			return nullptr;
		}
		success = head.compare_exchange_strong(dummy, new_dummy, std::memory_order_seq_cst);
	}while(!success);
	return dummy;
}

// Dequeues a batch of cnt_ui qEntries, or (x<cnt_ui entries until until_ts, and set_cnt_ui
// to the actual number of entries dequeued).
// If cnt_ui=0, then the restriction will just be until_ts.
QSTM::qEntry* QSTM::queue_t::dequeue_until(unsigned long until_ts, unsigned long& cnt_ui){
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


QSTM::writesetQSTM::~writesetQSTM(){
	delete [] writes;
}

// CAS the state to Writing, then do writes, then set the state to Complete.
int QSTM::do_writes_qstm(qEntry *q){
	State bar = State::NotWriting;
	State baz = State::Writing;
	if(!q->st.compare_exchange_strong(bar, baz, std::memory_order_seq_cst)) return 0; // Failure

	q->wset_p->do_writes();
	q->fset_p->do_frees();
	// On other architectures, a fence might be needed here
	q->st = State::Complete; // Release lock

	return 1; // Success
}

// NOTE This was modified to not use a testconfig. 128 threads hardcoded max.
//void QSTM::init(TestConfig* tc){
void QSTM::init(){
	queue = new queue_t();
	thread_cnt = 72;
	reservs = new paddedAtomic<uint64_t>[thread_cnt];
	for (int i = 0; i < thread_cnt; i++){
		reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
	}
	complete_snapshot.store(0, std::memory_order_release);
	min_reserv_snapshot.store(0, std::memory_order_release);
	queue->txncount.store(0);
}

QSTM::QSTM(TestConfig* tc, int tid) : TM(tid){
	nesting = 0;
	wset = new writesetQSTM();
	mset = new mallocset();
	fset = new freeset();
	my_txn = nullptr;
}

QSTM::~QSTM(){
	if (wset){
		delete(wset);
	}
	if (fset){
		delete(fset);
	}
	// // assert(mset);
	delete(mset);

}

// In new QSTM, the start value can simply be the newest transaction in the queue.
void QSTM::tm_begin(){
	nesting++; // A value of 1 indicates an ongoing transaction
	if(nesting > 1) return;

	if (wset == nullptr){
		wset = new writesetQSTM();
		fset = new freeset();
	}
	reserve(tid);

	start = queue->tail.load();

	return;
}

// Read is very complicated in the new QSTM
// It must check own wset first, and then the queue, and also validate/abort, and finally as a last resort read from memory.
void* QSTM::tm_read(void** addr){
//	assert(nesting != 0);
	void* look; // For returning writeset lookup values
	void* newestval; // For maintaining the newest version of the value through the traversal
	void* val; // For direct read from memory
	bool success = false; // For determining whether the queue contained the value

	if(wf.contains(addr)){
		if (wset->lookup((void**) addr, &look)) return look;
	}

	// Traverse from complete onwards to the tail.
	unsigned long startstamp = start->ts; // bound the first half of the traversal
	unsigned long tailstamp = queue->tail.load()->ts; // bound the second half of the traversal
	unsigned long prevts;

	qEntry *traverse; // start of traversal will be set further down

	// Read from memory. Either it is consistent or we will find a copy in the queue to return instead
	// This sequence solves the tricky problem of "what if another entry is added between the traversal start and this read?"
	qEntry *traverse_complete = queue->complete.load(); // This can move past an entry that we should have looked at for a new value!
	val = *addr;
	tailstamp = queue->tail.load()->ts;

	// Complete can pass start so we need to start from whichever one is older
	if(start->ts < traverse_complete->ts){
		traverse = start->next; // My start time is older than "complete"
	}else{
		traverse = traverse_complete->next; // My start time is newer than (or equal to) "complete"
	}
	// Traverse might be nullptr when (start==complete==tail) but I think that's fine -Alan

	// First loop, for reading up to "start"
	// Note that the next loop starts with (traverse == start->next), similar to check()
	while((traverse != nullptr) && (traverse->ts <= startstamp)){
		// Read step
		if(traverse->wf.contains(addr)){
			if(traverse->wset_p->lookup((void**) addr, &look)){
				success = true;
				newestval = look;
			}
		}
		traverse = traverse->next.load();
	}
	// Second loop, for reading and validating from "start" to the tail
	while((traverse != nullptr) && (traverse->ts <= tailstamp)){
		// Read step
		if(traverse->wf.contains(addr)){
			if(traverse->wset_p->lookup((void**) addr, &look)){
				success = true;
				newestval = look;
			}
		}

		// Validation step
		if(rf.intersect(traverse->wf)){
			tm_abort();
		}
		start = traverse; // This can be unconditional because of reading directly from wsets!

		traverse = traverse->next.load();
	}
	rf.add(addr); // Must do this before returning in any case (except when reading out of own wset)

	if(success) return newestval;

	// Value not found in queue, read from memory instead
	return val;
}

// This method doesn't need to change from original QSTM (or original RingSTM even)
void QSTM::tm_write(void** addr, void* val, void* mask){
//	assert(nesting != 0);
	wset->add(addr, val, mask);
	wf.add(addr);
	return;
}

// Committing thread should prepare the entry, CAS it in, persist pointer, fix tail, and become worker if necessary.
bool QSTM::tm_end(){
//	assert(nesting != 0); // Shouldn't ever call this function when nesting == 0
	nesting--; // A value of 0 indicates that the outermost transaction has ended
	if(nesting > 0) return true;

	// Read-only case
	if(wset->empty()){
		wf.clear();
		rf.clear();
		return true;
	}
	// Allocate a queue entry and populate it with the right info
	my_txn = get_new_qEntry();
	my_txn->wset_p = wset;
	my_txn->fset_p = fset;
	my_txn->wf = wf;

	qEntry *oldtail; // To fix the tail later

#ifdef DUR_LIN
	// Note: We only need to flush the {w,f}set, {w,f}set pointer, next pointer, and ts for recovery. State is flushed in do_writes_qstm()
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

			success = oldtail->next.compare_exchange_strong(foo, my_txn, std::memory_order_seq_cst);
		}else{
#ifdef DUR_LIN
			FLUSH(&(oldtail->next)); // Someone else might not have flushed this yet since the tail isn't fixed
			FLUSHFENCE;
#endif
			queue->tail.compare_exchange_strong(oldtail, oldtail->next.load(), std::memory_order_seq_cst); // Fix tail
		}
	}while(!success);
//	assert(my_txn->wset_p);
#ifdef DUR_LIN
	FLUSH(&(oldtail->next)); // We did it, so now we need to persist the CAS
	FLUSHFENCE;
#endif
	queue->tail.compare_exchange_strong(oldtail, my_txn, std::memory_order_seq_cst); // Fix tail

	// Periodic worker tasks
	wb_worker(); // This can probably be made less frequent but we would need to add a cleanup function for predictable results when testing

	if((my_txn->ts >= 50000) &&
		((my_txn->ts - queue->head.load()->ts)%50000 == (((unsigned int)rand()) % 500))) gc_worker(); // avoid multiple workers wake up together.

	// Since our wset and fset are globally accessible, we must leave them for a worker to clean up.
	wset = nullptr;
	fset = nullptr;
	my_txn = nullptr;
	mset->clear(); // We only needed this for aborts
	wf.clear();
	rf.clear();
	release(tid);
	return true;
}

// Clears all transaction data without doing any writes
void QSTM::tm_clear(){
	wf.clear();
	rf.clear();
	wset->clear();
	fset->clear();
	if (my_txn != nullptr){
		my_txn->detach();
		delete(my_txn);
		my_txn = nullptr;
	}
	mset->undo_mallocs();
	release(tid);
	
	return;
}

// Jumps back to outermost tm_begin() call after resetting things as needed
void QSTM::tm_abort(){
	tm_clear();
	nesting = 0;
	longjmp(env,1);
}

void* QSTM::tm_pmalloc(size_t size){
//	assert(nesting != 0);
	void *ptr = pstm_pmalloc(size);
	mset->add((void**)ptr);
	return ptr;
}

void* QSTM::tm_malloc(size_t size){
//	assert(nesting != 0);
	void *ptr = malloc(size);
	mset->add((void**)ptr);
	return ptr;
}

void QSTM::tm_pfree(void **ptr){
//	assert(nesting != 0);
	fset->add(ptr);
}

void QSTM::tm_free(void **ptr){
//	assert(nesting != 0);
	fset->add(ptr);
}

// Aborts if there is a conflict, and otherwise adjusts start time as needed
// Although this is only used when ending a transaction, updating start is still important since multiple attempts may be made
void QSTM::check(){
	unsigned long tailstamp = queue->tail.load()->ts;
	unsigned long prevts;
	if(tailstamp == start->ts) return;

	qEntry *traverse = start->next;

	while((traverse != nullptr) && (traverse->ts <= tailstamp)){
		if(rf.intersect(traverse->wf)){
			tm_abort();
			return;
		}
		start = traverse; // This becomes unconditional because of reading directly from wsets. (used to be suffix_end)

		traverse = traverse->next.load();
	}

	return;
}

//try to dequeue the head of the queue and reuse it.
//allocate new queue entry if no completed queue entry is available.
QSTM::qEntry* QSTM::get_new_qEntry(){
	qEntry* reused = queue->dequeue_one_until(min_reserv_snapshot.load(std::memory_order_acquire));
	if (reused == nullptr){
		return new qEntry();
	}
	reused->init();
	return reused;
	// return new qEntry();
} 

// Periodic worker task for writeback.
// Writes back all "NotWriting" queue entries
// Gives up if a CAS fails since another worker is running
void QSTM::wb_worker(){
	qEntry *traverse = queue->complete.load()->next; // Complete always points to a complete node
	unsigned long tailstamp = queue->tail.load()->ts; // bound the traversal
	// while((traverse != nullptr) && traverse->ts <= tailstamp){
	while(true){
		if (traverse == nullptr){
			break;
		}
		uint64_t traverse_ts = traverse->ts;
		if (traverse_ts > tailstamp){
			break;
		}
		if(!do_writes_qstm(traverse)) return; // Give up if CAS fails
		if ((traverse_ts & INTERVAL_MASK) == traverse_ts){ // do this once in a while
			complete_snapshot.store(traverse_ts, std::memory_order_release);
			min_reserv_snapshot.store(get_min_reserve(), std::memory_order_release);
		}
		queue->complete.store(traverse);
		traverse = traverse->next;
	}
	return;
}

// Performs memory management tasks
void QSTM::gc_worker(){
	qEntry *foo;
	qEntry *prev = nullptr;
	// unsigned long finish = queue->complete.load(std::memory_order_acquire)->ts;
	// dequeue until the current snapshot of lowest reservation.
	unsigned long finish = min_reserv_snapshot.load(std::memory_order_acquire);

	unsigned long batch_size;
	while(true){
		batch_size = 500;
		foo = queue->dequeue_until(finish, batch_size);
		for (unsigned long i = 0; i < batch_size; i++){
			prev = foo;
			foo = foo->next.load();
			prev->~qEntry();
		}
		if(batch_size < 500){
			return;
		}
	}
	return;
}

void QSTM::reserve(int tid){
	reservs[tid].ui.store(complete_snapshot.load(std::memory_order_acquire),
		std::memory_order_seq_cst);
}

uint64_t QSTM::get_min_reserve(){
	uint64_t min_ts = UINT64_MAX;
	for (int i = 0; i<thread_cnt; i++){
		uint64_t res = reservs[i].ui.load(std::memory_order_acquire);
		if(res<min_ts){
			min_ts = res;
		}
	}
	return min_ts;
}

void QSTM::release(int tid){
	reservs[tid].ui.store(UINT64_MAX, std::memory_order_seq_cst);
}


paddedAtomic<uint64_t>* QSTM::reservs = nullptr;
std::atomic<uint64_t> QSTM::complete_snapshot;
std::atomic<uint64_t> QSTM::min_reserv_snapshot;
int QSTM::thread_cnt;
QSTM::queue_t* QSTM::queue = nullptr;

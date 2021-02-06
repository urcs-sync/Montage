#include "ringstm.hpp"
#include "util/writeset.hpp"
#include "util/mallocset.hpp"
#include "util/freeset.hpp"
#include <setjmp.h>
#include <unistd.h>
#include <cstdlib>

#define likely(x) __builtin_expect ((x), 1)
#define unlikely(x) __builtin_expect ((x), 0)

// The global ring, visible to all threads
ringbuf ring;

RingSTM::RingSTM(TestConfig* tc, int tid) : TM(tid){
	wset = new writeset();
	mset = new mallocset();
	fset = new freeset();
}

void RingSTM::init(TestConfig* tc){
}

void RingSTM::tm_begin(){
	nesting++; // A value of 1 indicates an ongoing transaction
	if(nesting > 1) return;

	start = ring.ring_index.ui.load();

	// Decrease start to skip recent entries which are pending a ts update (should be at most 1)
	if(ring.ring[start%RING_SIZE].ts < start) start--;

	// Decrease start to cover at least one complete transaction
	while(ring.ring[start%RING_SIZE].st == State::Writing) start--;

	asm volatile ("lfence" ::: "memory");

	return;
}

void* RingSTM::tm_read(void** addr){
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

void RingSTM::tm_write(void** addr, void* val, void *mask){
	assert(nesting != 0);
	wset->add(addr, val, mask);
	wf.add(addr);
	return;
}

// Attempts to end a transaction, and clears all transaction data
bool RingSTM::tm_end(){
	assert(nesting != 0); // Shouldn't ever call this function when nesting == 0
	nesting--; // A value of 0 indicates that the outermost transaction has ended
	if(nesting > 0) return true;

	// Read-only case
	if(wset->empty()){
		wf.clear();
		rf.clear();
		return true;
	}

	bool success;
	unsigned long commit_time;
	do{
		commit_time = ring.ring_index.ui.load();
		check(); // This does not return until the newest entry has a current timestamp

		// If +2 is Complete then +1 is Complete (Must always retain at least one completed entry and we are about to use +1)
		while(ring.ring[(commit_time+2)%RING_SIZE].st != State::Complete){
			asm volatile("" ::: "memory"); // Prevent compiler from optimizing the loop
		}
		success = ring.ring_index.ui.compare_exchange_strong(commit_time, (commit_time+1), std::memory_order_seq_cst);
//		if(!success){
//			unsigned int rand_microsec = BACKOFF_BASE;
//			unsigned int factor = (rand() % BACKOFF_FACTOR)+1;
//			rand_microsec *= factor;
//			usleep(rand_microsec);
//		}
	}while(!success);
	ring.ring[(commit_time+1)%RING_SIZE].st = State::Writing;
	ring.ring[(commit_time+1)%RING_SIZE].wf = wf;

#ifdef DUR_LIN
	// NOTE We only need to persist the timestamp, the state, the freeset, and the writeset for recovery.
	wset->flush(); // Persist write set before the pointer
	fset->flush(); // Persist free set before the pointer
	ring.ring[(commit_time+1)%RING_SIZE].wset_p = wset;
	ring.ring[(commit_time+1)%RING_SIZE].fset_p = fset;
	FLUSH(&(ring.ring[(commit_time+1)%RING_SIZE].wset_p));
	FLUSH(&(ring.ring[(commit_time+1)%RING_SIZE].fset_p));
	FLUSH(&(ring.ring[(commit_time+1)%RING_SIZE].st));

//std::cout << "Addr of ts is " << (void*)&ring.ring[(commit_time+1)%RING_SIZE].ts << std::endl;
//std::cout << "Addr of wf is " << &ring.ring[(commit_time+1)%RING_SIZE].wf << std::endl;
//std::cout << "Addr of st is " << (void*)&ring.ring[(commit_time+1)%RING_SIZE].st << std::endl;

//	for(unsigned int i=0; (i*32)<FILTER_SIZE; i++){
//		void** foo = (void**)((char*)(&ring.ring[(commit_time+1)%RING_SIZE].wf)+(i*32));
//std::cout << "flushing:" << foo << std::endl;
//		FLUSH(foo);
//	}
//	FLUSH(&(ring.ring_index.ui));
	FLUSHFENCE;
#endif

	//WAW Fence (not needed on x86)
	asm volatile("" ::: "memory"); // Prevent compiler reordering
	ring.ring[(commit_time+1)%RING_SIZE].ts = (commit_time+1);

#ifdef DUR_LIN
	FLUSH(&(ring.ring[(commit_time+1)%RING_SIZE].ts));
	FLUSHFENCE;
#endif// This is the point where a commit in the ring is fully durable

	// Ensure that overlapping writes are correctly ordered
	for(unsigned int i = commit_time; i > start; i--){
		if(wf.intersect(ring.ring[i%RING_SIZE].wf)){
			while(ring.ring[i%RING_SIZE].st != State::Complete){
				asm volatile("" ::: "memory"); // Prevent compiler from optimizing the loop
			}
		}
	}

	asm volatile ("mfence" ::: "memory");
	wset->do_writes();

	// Wait for prior entry to complete
	while(ring.ring[commit_time%RING_SIZE].st != State::Complete){
		asm volatile("" ::: "memory"); // Prevent compiler from optimizing the loop
	}

	asm volatile ("mfence" ::: "memory");
	ring.ring[(commit_time+1)%RING_SIZE].st = State::Complete;

#ifdef DUR_LIN
	FLUSH(&(ring.ring[(commit_time+1)%RING_SIZE].st));
	FLUSHFENCE;
#endif

	fset->do_frees(); // Do frees which were deferred inside the transaction (this clears it too)
	tm_clear(); // clear the data
	mset->clear(); // We only needed this for aborts
	return true;
}

// Clears all transaction data without doing any writes
void RingSTM::tm_clear(){
	nesting = 0;
	wset->clear();
	wf.clear();
	rf.clear();
	return;
}

// Jumps back to outermost tm_begin() call after resetting things as needed
void RingSTM::tm_abort(){
	mset->undo_mallocs();
	fset->clear();
	tm_clear();

//	unsigned int rand_microsec = BACKOFF_BASE;
//	unsigned int factor = (rand() % BACKOFF_FACTOR)+1;
//	rand_microsec *= factor;
//	usleep(rand_microsec);

	longjmp(env,1);
}

//TODO Use correct allocator, and correct free() in mallocset.cpp
void* RingSTM::tm_malloc(size_t size){
	assert(nesting != 0);
	void *ptr = pstm_pmalloc(size);
	mset->add((void**)ptr);
	return ptr;
}

//TODO Use correct free() in freeset.cpp
void RingSTM::tm_free(void **ptr){
	assert(nesting != 0);
	fset->add(ptr);
}

// Sets the transaction abort flag if there is a conflict, and otherwise adjusts start time as needed
void RingSTM::check(){
	unsigned long suffix_end = ring.ring_index.ui.load();
	if(suffix_end == start) return;

	while(ring.ring[suffix_end%RING_SIZE].ts < suffix_end){
		asm volatile("" ::: "memory"); // Prevent compiler from optimizing the loop
	}
	for(unsigned long i = suffix_end; i > start; i--){
		if(rf.intersect(ring.ring[i%RING_SIZE].wf)){
			tm_abort();
			return;
		}
		if(ring.ring[i%RING_SIZE].st != State::Complete) suffix_end = i-1;
	}
	unsigned long old_start = start;
	start = suffix_end;

	// Detect long context switch and abort
	if(ring.ring[old_start%RING_SIZE].ts != old_start){
		tm_abort();
	}
	return;
}


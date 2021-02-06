#include <iostream>
#include "../util/ringbuf.hpp"

void show(ringbuf *ring){
	for(unsigned int i=0; i<RING_SIZE; i++){
		std::cout << "[" << ring->ring[i].ts << "] ";
	}
	std::cout << std::endl;
}

// Tries to add a ring entry until it succeeds
void insert(ringbuf * ring, entry foo){
	unsigned long commit_time;
	bool success = false;
	do{
		commit_time = ring->ring_index.load();
		success = ring->ring_index.compare_exchange_strong(commit_time, (commit_time+1), std::memory_order_seq_cst);
	}while(success == false);
	ring->ring[(commit_time+1)%RING_SIZE] = foo; // Copy entry into ring
}

//TODO: Multithreaded steady-state tests
int main(){
	ringbuf a;

	entry foo;
	for(int i=0; i<RING_SIZE+1; i++){
		foo.ts = i;
		insert(&a, foo);
	}
	show(&a);

	return 0;
}

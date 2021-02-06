#include "writeset.hpp"
#include "../config.hpp"
#include <cstdlib>
#include <iostream>

void writeset::init(){
	max = 16;
	size = 0;
	writes = new write_record[max];
}

writeset::writeset(){
	init();
}

bool writeset::lookup(void **addr, void **ret){
	if (size == 0) return false;
	unsigned int i = size;
	do{
		i--;
		if (writes[i].addr == addr){
			if(writes[i].mask == 0){
				*ret = writes[i].val;
				return true;
			}else{
				// Read current value
				void *oldval = *(writes[i].addr);

				// apply inverse mask
				oldval = (void*) ((uintptr_t)oldval & (~(uintptr_t)(writes[i].mask)));

				// Apply mask to val
				void *newval = (void*)((uintptr_t)writes[i].val & (uintptr_t)writes[i].mask);

				// Merge
				*ret = (void*)((uintptr_t)newval & (uintptr_t)oldval);

				return true;
			}
		}
	}while(i!=0);

	return false;
}

// Add a new entry and possibly reallocate
void writeset::add(void **addr, void *val, void *mask){
	if (++size > max){ // re-allocation needed.
		unsigned int new_max = max * 2;
		write_record* temp = new write_record[new_max];
		std::copy(writes, writes+max, temp);
		delete [] writes;
		writes = temp;
		max = new_max;
	}
	writes[size-1].addr = addr;
	writes[size-1].val = val;
	writes[size-1].mask = mask;
}

void writeset::do_writes(){
	for (unsigned int i = 0; i < size; i++){
		if(writes[i].mask == 0){
			*(writes[i].addr) = writes[i].val;
		}else{
			// Read current value
			void *oldval = *(writes[i].addr);

			// apply inverse mask
			oldval = (void*) ((uintptr_t)oldval & (~(uintptr_t)(writes[i].mask)));

			// Apply mask to val
			void *newval = (void*)((uintptr_t)writes[i].val & (uintptr_t)writes[i].mask);

			// Merge
			newval = (void*)((uintptr_t)newval & (uintptr_t)oldval);

			// write
			*(writes[i].addr) = newval;
		}
	}
#ifdef DUR_LIN
	for(unsigned int i = 0; i < size; i++){
		FLUSH(writes[i].addr);
	}
	FLUSHFENCE;
#endif
	return;
}

bool writeset::empty(){
	return (size == 0);
}

void writeset::clear(){
	if(max > 128){
		delete [] writes;
		init();
	}else size=0;
}

void writeset::flush(){
#ifdef DUR_LIN
	FLUSH(this);
	for(unsigned int i = 0; i < size; i++){
		FLUSH(&writes[i]);
	}
// We rely on a fence following the function call
//	FLUSHFENCE;
#endif
}

writeset::~writeset(){
}

#include "mallocset.hpp"
#include "../config.hpp"
#include <cstdlib>
#include <iostream>

void mallocset::init(){
	max = 16;
	size = 0;
	mallocs = new malloc_record[max];
}

mallocset::mallocset(){
	init();
}

// Add a new entry and possibly reallocate
void mallocset::add(void **addr){
	if (++size > max){ // re-allocation needed.
		unsigned int new_max = max * 2;
		malloc_record* temp = new malloc_record[new_max];
		std::copy(mallocs, mallocs+max, temp);
		delete [] mallocs;
		mallocs = temp;
		max = new_max;
	}
	mallocs[size-1].addr = addr;
}

void mallocset::undo_mallocs(){
	for (unsigned int i = 0; i < size; i++){
		if(RP_in_prange(mallocs[i].addr))
			pstm_pfree((void*)mallocs[i].addr);
		else//this addr is in transient memory
			free((void*)mallocs[i].addr);
	}
	clear(); // Don't want any double frees!
	return;
}

bool mallocset::empty(){
	return (size == 0);
}

void mallocset::clear(){
	if(max > 128){
		delete [] mallocs;
		init();
	}else size=0;
}

void mallocset::flush(){
#ifdef DUR_LIN
	FLUSH(this);
	for(unsigned int i = 0; i < size; i++){
		FLUSH(&mallocs[i]);
	}
// We rely on a fence following the function call
#endif
}

mallocset::~mallocset(){
	delete(mallocs);
}

#include "freeset.hpp"
#include "../config.hpp"
#include <cstdlib>
#include <iostream>

void freeset::init(){
	max = 16;
	size = 0;
	frees = new free_record[max];
}

freeset::freeset(){
	init();
}

// Add a new entry and possibly reallocate
void freeset::add(void **addr){
	if (++size > max){ // re-allocation needed.
		unsigned int new_max = max * 2;
		free_record* temp = new free_record[new_max];
		std::copy(frees, frees+max, temp);
		delete [] frees;
		frees = temp;
		max = new_max;
	}
	frees[size-1].addr = addr;
}

void freeset::do_frees(){
	for (unsigned int i = 0; i < size; i++){
		// delete frees[i].addr; // TODO
		if(frees[i].addr>base_addr && frees[i].addr < base_addr+MAKALU_FILESIZE)//this addr is in NVM
			pstm_pfree((void*)frees[i].addr);
		else//this addr is in transit memory
			free((void*)frees[i].addr);
	}
	clear(); // Don't want any double frees!
	return;
}

bool freeset::empty(){
	return (size == 0);
}

void freeset::clear(){
	if(max > 128){
		delete [] frees;
		init();
	}else size=0;
}

void freeset::flush(){
#ifdef DUR_LIN
	FLUSH(this);
	for(unsigned int i = 0; i < size; i++){
		FLUSH(&frees[i]);
	}
// We rely on a fence following the function call
//	FLUSHFENCE;
#endif
}

freeset::~freeset(){
	delete(frees);
}

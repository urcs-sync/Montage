#include <cstring>
#include "bloomfilter.hpp"
#include <cstdint>

filter::filter(){
	clear();
}

// Given another filter, determines if there is an intersection
bool filter::intersect(filter other){
#ifdef USE_PREFILTER
	bool pre = false;
	for(int i=0; i<PREFILTER_SIZE; i=i+8){
		if((*((uint64_t*)(preBitTable+i)) & *((uint64_t*)(other.preBitTable+i))) != 0){
			pre = true;
			break;
		}
	}
	if(pre){
#endif
		for(int i=0; i<FILTER_SIZE; i=i+8){
			if((*((uint64_t*)(bitTable+i)) & *((uint64_t*)(other.bitTable+i))) != 0){
				return true;
			}
		}
		return false;
#ifdef USE_PREFILTER
	}else return false;
#endif
}

// Adds an entry to the filter by BloomHashing it
void filter::add(void *ptr){
#ifdef USE_PREFILTER
	int preBitIndex = PreBloomHash(ptr);
	preBitTable[preBitIndex / 8] |= 1 << preBitIndex % 8;
#endif

	int bitIndex = BloomHash(ptr);
	bitTable[bitIndex / 8] |= 1 << bitIndex % 8;
}

// Clears the bit table
void filter::clear(){
#ifdef USE_PREFILTER
	std::memset(preBitTable, 0, PREFILTER_SIZE);
#endif
	std::memset(bitTable, 0, FILTER_SIZE);
}


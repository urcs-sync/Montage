#ifndef FILTER_H
#define FILTER_H

#include "../../config.hpp"
#define BloomHash(ptr) (((unsigned long)ptr>>3) % (FILTER_SIZE*8))
#define PreBloomHash(ptr) (((unsigned long)ptr>>3) % (PREFILTER_SIZE*8))

class filter{
	public:
	filter();

	bool intersect(filter other);

	//inline bool contains(void *ptr);
	inline bool contains(void *ptr){
		int bitIndex = BloomHash(ptr);
		return (0 != ((bitTable[bitIndex / 8]) & (1 << bitIndex % 8)) );
	}

	// Adds an entry to the filter by BloomHashing it
	void add(void* ptr);
                 
	// Clears the bit table
	void clear();

	private:
	unsigned char bitTable[FILTER_SIZE];
#ifdef USE_PREFILTER
	unsigned char preBitTable[FILTER_SIZE];
#endif
};
#endif

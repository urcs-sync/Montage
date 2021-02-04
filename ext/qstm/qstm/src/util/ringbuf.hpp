#ifndef RINGBUF_H
#define RINGBUF_H

#include <atomic>
// #include "bloomfilter.hpp"
#include "../config.hpp"
// #ifdef DUR_LIN
// 	#include "writeset.hpp"
// #endif
#include "entry.hpp"
#include "concurrentprimitives.hpp"

class ringEntry : public Entry{
public:
	ringEntry();
#ifdef DUR_LIN
	writeset *wset_p;
	freeset *fset_p;
#endif
};

class ringbuf{
public:
	ringbuf();
	ringEntry ring[RING_SIZE];
	padded<std::atomic<unsigned long>> ring_index; // Points to the newest ringEntry
};

#endif

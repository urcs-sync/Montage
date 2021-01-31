#ifndef CONFIG
#define CONFIG

// For bloom filters (number of 8 byte words)
//#define USE_PREFILTER
//#define FILTER_SIZE 8			// Optimal for stack
//#define FILTER_SIZE 32		// Optimal for queue
//#define FILTER_SIZE 1024		// Optimal for list
// #define FILTER_SIZE 128		// Optimal for map
//#define FILTER_SIZE 128		// Optimal for STAMP intruder
#define FILTER_SIZE 2048		// Default size
//#define FILTER_SIZE 8192		// Optimal for STAMP vacation

// Uncomment to enable durable linearizability
#define DUR_LIN

#include "pfence_util.h"
#define PWB_IS_CLFLUSH

#ifdef PWB_IS_CLFLUSH
	#define FLUSH(addr) asm volatile ("clflush (%0)" :: "r"(addr))
	#define FLUSHFENCE asm volatile ("sfence" ::: "memory")
#elif defined(PWB_IS_PCM)
	#define FLUSH(addr) emulate_latency_ns(340)
	#define FLUSHFENCE emulate_latency_ns(500)
#else
#error "Please define what PWB is."
#endif


#include <cstddef>

extern char *base_addr;
#include "ralloc.hpp"
#define pstm_pmalloc(size) RP_malloc(size)
#define pstm_pfree(addr) RP_free(addr)

#endif

#ifndef CONFIG
#define CONFIG

// For bloom filters (number of 8 byte words)
// Annotated with vacation performance (intruder is best with a smaller bloom filter since it has very small transactions)
//#define USE_PREFILTER
//#define FILTER_SIZE 8			// Optimal for stack
//#define FILTER_SIZE 32		// Optimal for Queue
//#define FILTER_SIZE 1024		// Optimal for list
//#define FILTER_SIZE 128		// Optimal for map
// #define FILTER_SIZE 128		// Optimal for intruder
//#define FILTER_SIZE 256		// Optimal for YCSB
//#define FILTER_SIZE 1024		// Optimal for TPC-C_SMALL
#define FILTER_SIZE 2048		// "Best guess at a compromise" size for all tests
//#define FILTER_SIZE 8192		// Optimal for vacation

// Number of entries in a ring
#define RING_SIZE 50000

// Delay when spinning (Currently unused)
//#define BACKOFF
#define BACKOFF_BASE 100 // usec
#define BACKOFF_FACTOR 500 // usec

// Max retry count in writesetQSTM::do_write()
#define DOWRITE_MAX 16

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

// #include "mtm.h"
// extern "C" void* mtm_pmalloc(size_t);
// extern "C" void mtm_pfree (void*);
// #include <cstdlib>
// #include "Romulus.hpp"
// #include "malloc.hpp"
// extern mspace Romulus_p_ms;
// extern const uint8_t* base_addr;
// extern const uint64_t max_size;

//#include "makalu.h"
extern char *base_addr;
//#define MAKALU_FILESIZE 5*1024*1024*1024ULL + 24
//#define pstm_pmalloc(size) MAK_malloc(size)
//#define pstm_pfree(addr) MAK_free(addr)
#include "ralloc.hpp"
#define pstm_pmalloc(size) RP_malloc(size)
#define pstm_pfree(addr) RP_free(addr)
// #define pstm_pfree(addr)

#endif

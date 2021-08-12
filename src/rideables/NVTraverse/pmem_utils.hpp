#ifndef NVTRAVERSE_PMEM_UTILS_HPP
#define NVTRAVERSE_PMEM_UTILS_HPP

#include <iostream>
#ifndef NVT_PWB_IS_CLWB
#define NVT_PWB_IS_CLWB 1
#endif

template <class ET>
inline bool NVT_CASB(ET volatile *ptr, ET oldv, ET newv) { 
	bool ret;
	if (sizeof(ET) == 1) { 
		ret = __sync_bool_compare_and_swap_1((bool*) ptr, *((bool*) &oldv), *((bool*) &newv));
	} else if (sizeof(ET) == 8) {
		ret = __sync_bool_compare_and_swap_8((long*) ptr, *((long*) &oldv), *((long*) &newv));
	} else if (sizeof(ET) == 4) {
		ret = __sync_bool_compare_and_swap_4((int *) ptr, *((int *) &oldv), *((int *) &newv));
	} 
#if defined(MCX16)
	else if (sizeof(ET) == 16) {
		ret = __sync_bool_compare_and_swap_16((__int128*) ptr,*((__int128*)&oldv),*((__int128*)&newv));
	}
#endif
	else {
		std::cout << "CAS bad length (" << sizeof(ET) << ")" << std::endl;
		abort();
	}
	return ret;
}

template <class ET>
inline ET NVT_CASV(ET volatile *ptr, ET oldv, ET newv) { 
	ET ret;
	if (sizeof(ET) == 1) { 
		std::cout << "CASV bad length (" << sizeof(ET) << ")" << std::endl;
		abort();
	} else if (sizeof(ET) == 8) {
		ret = (ET) __sync_val_compare_and_swap_8((long*) ptr, *((long*) &oldv), *((long*) &newv));
//return utils::LCAS((long*) ptr, *((long*) &oldv), *((long*) &newv));
	} else if (sizeof(ET) == 4) {
		std::cout << "CASV bad length (" << sizeof(ET) << ")" << std::endl;
		abort();
//return utils::SCAS((int *) ptr, *((int *) &oldv), *((int *) &newv));
	} 
#if defined(MCX16)
	else if (sizeof(ET) == 16) {
		ret = (ET) __sync_val_compare_and_swap_16((__int128*) ptr,*((__int128*)&oldv),*((__int128*)&newv));
	}
#endif
	else {
		std::cout << "CASV bad length (" << sizeof(ET) << ")" << std::endl;
		abort();
	}
	return ret;
}



#define MFENCE __sync_synchronize
#define SAME_CACHELINE(a, b) ((((uint64_t)(a))>>6) == (((uint64_t)(b))>>6))
const uint64_t NVT_CACHELINE_MASK = ~(64ULL-1ULL);

#define DUMMY_TID 0
#ifdef PMEM_STATS
	#define PMEM_STATS_PADDING 128
	#define MAX_THREADS_POW2 256
	static long long flush_count[MAX_THREADS_POW2*PMEM_STATS_PADDING]; //initialized to 0
	static long long fence_count[MAX_THREADS_POW2*PMEM_STATS_PADDING];

	void print_pmem_stats() {
		long long flush_count_agg = 0;
		long long fence_count_agg = 0;
		for(int i = 0; i < MAX_THREADS_POW2; i++) {
			flush_count_agg += flush_count[i*PMEM_STATS_PADDING];
			fence_count_agg += fence_count[i*PMEM_STATS_PADDING];
		}
		std::cout << "Flush count: " << flush_count_agg << std::endl;
		std::cout << "Fence count: " << fence_count_agg << std::endl;
	}
#endif

template <class ET>
	inline void NVT_FLUSH(const int tid, ET *p)
	{
	#ifdef PMEM_STATS
		flush_count[tid*PMEM_STATS_PADDING]++;
	#endif

	#ifdef NVT_PWB_IS_CLFLUSH
	#error "please use clwb!"
		asm volatile ("clflush (%0)" :: "r"(p));
	#elif NVT_PWB_IS_CLFLUSHOPT
	#error "please use clwb!"
		asm volatile ("clflushopt (%0)" :: "r"(p));
	#elif NVT_PWB_IS_CLWB
		asm volatile ("clwb (%0)" :: "r"(p));
	#else
	#error "You must define what PWB is. Choose PWB_IS_CLFLUSH if you don't know what your CPU is capable of"
	#endif
	}

// assumes that ptr + size will not go out of the struct
// also assumes that structs fit in one cache line when aligned
template <class ET>
	inline void NVT_FLUSH_STRUCT(const int tid, ET *ptr, size_t size)
	{
	#if defined(CACHE_ALIGN)
		NVT_FLUSH(tid, ptr);
	#else
		//cout << "FLUSH_STRUCT(" << (uint64_t) ptr << " " << size << ")" << endl;
		for(uint64_t p = ((uint64_t) ptr)&NVT_CACHELINE_MASK; p < ((uint64_t) ptr) + size; p += 64ULL)
			//cout << p << endl;
			NVT_FLUSH(tid, (void*) p);
	#endif
	}	

template <class ET>
	inline void NVT_FLUSH_STRUCT(const int tid, ET *ptr)
	{
	#if defined(CACHE_ALIGN)
		NVT_FLUSH(tid, ptr);
	#else
		NVT_FLUSH_STRUCT(tid, ptr, sizeof(ET));
	#endif
	//for(char *p = (char *) ptr; (uint64_t) p < (uint64_t) (ptr+1); p += 64)
	//	NVT_FLUSH(p);
	}	

// flush word pointed to by ptr in node n
template <class ET, class NODE_T>
	inline void NVT_FLUSH_node(const int tid, ET *ptr, NODE_T *n)
	{
	//if(!SAME_CACHELINE(ptr, n))
	//	std::cerr << "FLUSH NOT ON SAME_CACHELINE" << std::endl;
	#ifdef MARK_FLUSHED
		if(n->flushed)
			NVT_FLUSH(tid, ptr);
	#else
		NVT_FLUSH(tid, ptr);
	#endif
	}

// flush entire node pointed to by ptr
template <class ET>
	inline void NVT_FLUSH_node(const int tid, ET *ptr)
	{
	#ifdef MARK_FLUSHED
		if(ptr->flushed)
			NVT_FLUSH_STRUCT(tid, ptr);
	#else
		NVT_FLUSH_STRUCT(tid, ptr);
	#endif
	}


	inline void NVT_SFENCE()
	{
		asm volatile ("sfence");
	}

	inline void NVT_FENCE(const int tid)
	{
	#ifdef PMEM_STATS
		fence_count[tid*PMEM_STATS_PADDING]++;
	#endif

	#ifdef NVT_PWB_IS_CLFLUSH
		//MFENCE();
	#elif NVT_PWB_IS_CLFLUSHOPT
		NVT_SFENCE();
	#elif NVT_PWB_IS_CLWB
		NVT_SFENCE();
	#else
	#error "You must define what PWB is. Choose PWB_IS_CLFLUSH if you don't know what your CPU is capable of"
	#endif
	}

#define BARRIER(tid, p) {NVT_FLUSH(tid, p);NVT_FENCE(tid);}

#ifdef IZ
	#define IF_IZ if(1)
	#define IF_OURS if(0)
#else
	#define IF_OURS if(1)
	#define IF_IZ if(0)
#endif

	namespace pmem_utils {
	// The conditional should be removed by the compiler
	// this should work with pointer types, or pairs of integers

	template <class ET>
		inline bool NVT_FCAS(const int tid, ET volatile *ptr, ET oldv, ET newv) { 
			bool ret;
			if (sizeof(ET) == 1) { 
				ret = __sync_bool_compare_and_swap_1((bool*) ptr, *((bool*) &oldv), *((bool*) &newv));
			} else if (sizeof(ET) == 8) {
				ret = __sync_bool_compare_and_swap_8((long*) ptr, *((long*) &oldv), *((long*) &newv));
			} else if (sizeof(ET) == 4) {
				ret = __sync_bool_compare_and_swap_4((int *) ptr, *((int *) &oldv), *((int *) &newv));
			} 
#if defined(MCX16)
			else if (sizeof(ET) == 16) {
				ret = __sync_bool_compare_and_swap_16((__int128*) ptr,*((__int128*)&oldv),*((__int128*)&newv));
			}
#endif
			else {
				std::cout << "CAS bad length (" << sizeof(ET) << ")" << std::endl;
				abort();
			}
			NVT_FLUSH(tid, ptr);
#if defined(IZ) && !defined(MARK_FLUSHED)
			NVT_FENCE(tid);
#endif
			return ret;
		}

	template <class ET>
		inline ET NVT_FCASV(const int tid, ET volatile *ptr, ET oldv, ET newv) { 
			ET ret;
			if (sizeof(ET) == 1) { 
				std::cout << "CASV bad length (" << sizeof(ET) << ")" << std::endl;
				abort();
			} else if (sizeof(ET) == 8) {
				ret = (ET) __sync_val_compare_and_swap_8((long*) ptr, *((long*) &oldv), *((long*) &newv));
		//return utils::LCAS((long*) ptr, *((long*) &oldv), *((long*) &newv));
			} else if (sizeof(ET) == 4) {
				std::cout << "CASV bad length (" << sizeof(ET) << ")" << std::endl;
				abort();
		//return utils::SCAS((int *) ptr, *((int *) &oldv), *((int *) &newv));
			} 
	#if defined(MCX16)
			else if (sizeof(ET) == 16) {
				ret = (ET) __sync_val_compare_and_swap_16((__int128*) ptr,*((__int128*)&oldv),*((__int128*)&newv));
			}
	#endif
			else {
				std::cout << "CASV bad length (" << sizeof(ET) << ")" << std::endl;
				abort();
			}
			NVT_FLUSH(tid, ptr);
#if defined(IZ) && !defined(MARK_FLUSHED)
			NVT_FENCE(tid);
#endif
			return ret;
		}

	template <class ET>
		inline ET NVT_READ(const int tid, ET &ptr)
		{
			ET ret = ptr;
			NVT_FLUSH(tid, &ptr);
		#ifdef IZ
			NVT_FENCE(tid);
		#endif
			return ret;
		}

	template <class ET>
		inline void NVT_WRITE(const int tid, ET volatile &ptr, ET val)
		{
			NVT_FENCE(tid);
			ptr = val;
			NVT_FLUSH(tid, &ptr);
		}
/*
	// NO_FENCE operations are used at the beginning of every operation
	// Need to also remember to FENCE before returning.
	template <class ET>
		inline ET READ_NO_FENCE(const int tid, ET &ptr)
		{
			ET ret = ptr;
			NVT_FLUSH(tid, &ptr);
			return ret;
		}
*/

	template <class ET>
		inline void WRITE_NO_FENCE(const int tid, ET volatile &ptr, ET val)
		{
			ptr = val;
			NVT_FLUSH(tid, &ptr);
		}

	template <class ET, class NODE_T>
		inline ET NVT_READ_node(const int tid, ET &ptr, NODE_T *n)
		{
			ET ret = ptr;
			NVT_FLUSH_node(tid, &ptr, n);
		#ifdef IZ
			NVT_FENCE(tid);
		#endif
			return ret;
		}

/*
	template <class ET, class NODE_T>
	inline ET READ_NO_FENCE_node(const int tid, ET &ptr, NODE_T *n)
	{
		ET ret = ptr;
		FLUSH_node(tid, &ptr, n);
		return ret;
	}
*/
	template <class ET, class NODE_T>
		inline void NVT_WRITE_node(const int tid, ET volatile &ptr, ET val, NODE_T *n)
		{
		//if(!SAME_CACHELINE(&ptr, n))
		//	std::cerr << "WRITE NOT ON SAME_CACHELINE" << std::endl;
		#ifdef MARK_FLUSHED
			__sync_fetch_and_add(&n->flushed, 1);
		#endif
			WRITE(tid, ptr, val);
		#ifdef MARK_FLUSHED
			__sync_fetch_and_add(&n->flushed, -1);
		#endif
		}

	template <class ET, class NODE_T>
		inline bool NVT_FCAS_node(const int tid, ET volatile *ptr, ET oldv, ET newv, NODE_T *n) { 
		#ifdef MARK_FLUSHED
			__sync_fetch_and_add(&n->flushed, 1);
		#endif
			bool ret = FCAS(tid, ptr, oldv, newv);
		#ifdef MARK_FLUSHED
			__sync_fetch_and_add(&n->flushed, -1);
		#endif
			return ret;
		}

	template <class ET, class NODE_T>
		inline ET NVT_FCASV_node(const int tid, ET volatile *ptr, ET oldv, ET newv, NODE_T *n) { 
		//if(!SAME_CACHELINE(ptr, n))
		//	std::cerr << "FCASV NOT ON SAME_CACHELINE" << std::endl;
		#ifdef MARK_FLUSHED
			__sync_fetch_and_add(&n->flushed, 1);
		#endif
			ET ret = FCASV(tid, ptr, oldv, newv);
		#ifdef MARK_FLUSHED
			__sync_fetch_and_add(&n->flushed, -1);
		#endif
			return ret;
		}

	/*
	typedef struct
    {
      unsigned __int128 value;
    } __attribute__ ((aligned (16))) atomic_uint128;*/

/*
    unsigned __int128 atomic_read_uint128 (unsigned __int128 *src)
    {
      if((unsigned long long) src & 15ull) cerr << src << " is not 16 byte aligned" << endl;
      unsigned __int128 result;
      asm volatile ("xor %%rax, %%rax;"
                    "xor %%rbx, %%rbx;"
                    "xor %%rcx, %%rcx;"
                    "xor %%rdx, %%rdx;"
                    "lock cmpxchg16b %1" : "=A"(result) : "m"(*src) : "rbx", "rcx");
      return result;
    }*/
	}
	
#endif /* PMEM_UTILS_H_ */


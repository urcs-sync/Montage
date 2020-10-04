/*

Copyright 2015 University of Rochester

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. 

*/



#ifndef PERSIST_FUNC_HPP
#define PERSIST_FUNC_HPP

// #include "sysextend.h"

namespace persist_func{
	inline void clflush(void *p){
		asm volatile ("clflush (%0)" :: "r"(p));
	}

	inline void clflushopt(void *p){
		asm volatile ("clflushopt (%0)" :: "r"(p));
	}

	inline void clwb(void *p){
		asm volatile ("clwb (%0)" :: "r"(p));
	}

	inline void mfence(){
		asm volatile ("mfence");
	}

	inline void sfence(){
		asm volatile ("sfence");
	}

	inline void clflush_range_nofence(void *p, size_t sz){// unit of sz is byte.
		for(char* curr = (char*)p; curr <= (char*)(((size_t)p+sz)|CACHE_LINE_MASK); curr += CACHE_LINE_SIZE){
			clflushopt(curr);
		}
	}

	inline void clflush_range(void *p, size_t sz){// unit of sz is byte.
		clflush_range_nofence(p,sz);
		sfence();
	}

	inline void clwb_range_nofence(void *p, size_t sz){
		for(char* curr = (char*)p; curr <= (char*)(((size_t)p+sz)|CACHE_LINE_MASK); curr += CACHE_LINE_SIZE){
			clwb(curr);
		}
	}

	inline void clwb_range(void *p, size_t sz){
		clwb_range_nofence(p,sz);
		sfence();
	}

	inline void wholewb(){
		//sysextend(__NR_whole_cache_flush, NULL);
		return;
	}

	inline void flush_fence(void *p){
		clwb(p);
		sfence();
	}

}

#endif

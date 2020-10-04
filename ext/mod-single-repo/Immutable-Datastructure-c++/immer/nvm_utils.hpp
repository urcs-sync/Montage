//
// immer: immutable data structures for C++
// Author: Swapnil Haria <swapnilh@cs.wisc.edu> 
//
//

#pragma once

#include <x86intrin.h>
#include <iostream>
#include <array>
#include <cassert>

/*
void* operator new(size_t size) {
    return immer::default_memory_policy::heap::type::allocate(size);
}

void operator delete(void* data) {
    immer::default_memory_policy::heap::type::deallocate(0, data);
}

void operator delete(void* data, size_t size) {
    immer::default_memory_policy::heap::type::deallocate(size, data);
}
*/
namespace immer {

constexpr auto CACHE_LINE_SIZE = 64;
constexpr auto SET_SIZE = 100;
//std::unordered_set<uint64_t> lines_to_flush;
static int cnt = 0;

struct FlushOps {

    static void persist_range(const void *ptr, uint64_t len, bool finalize) {
        static std::array<uint64_t, SET_SIZE> lines_to_flush;
        if (finalize == false) {
            //    std::cout << "Flushing addr:" << ptr << ", size:" << len << std::endl;
            uintptr_t start = (uintptr_t)ptr & ~(CACHE_LINE_SIZE-1);
            bool unique = true;
            for (; (char*)start < (char*)ptr + len; start += CACHE_LINE_SIZE) {
                //_mm_clwb((void*)start);
                //lines_to_flush.insert(start);
                for (int i = 0; i < cnt; i++) {
                    if (lines_to_flush[i] == start) {
                        unique = false;
                        break;
                    }
                } 
                if (unique) {
                    assert(cnt < SET_SIZE);
                    lines_to_flush[cnt] = start;
                    cnt++;
                }
            }
        } else {
            for (int i=0; i < cnt; i++) {
#ifdef PMEM_ENABLED
                _mm_clwb((void*)lines_to_flush[i]);
#else
                //_mm_clflushopt((void*)lines_to_flush[i]);
                _mm_clwb((void*)lines_to_flush[i]);
#endif
            }
            cnt = 0;
        }
    }
 
    static void persist_range_now(const void *ptr, uint64_t len) {
        //    std::cout << "Flushing addr:" << ptr << ", size:" << len << std::endl;
        uintptr_t start = (uintptr_t)ptr & ~(CACHE_LINE_SIZE-1);
        for (; (char*) start < (char*)ptr + len; start += CACHE_LINE_SIZE) {
#ifdef PMEM_ENABLED
            _mm_clwb((void*)start);
#else
            //_mm_clflushopt((void*)start);
            _mm_clwb((void*)start);
#endif
        }
    }

    static void persist_fence() {
        _mm_sfence();
    }
    
};

#ifndef IMMER_DISABLE_FLUSHING
#define NVM_PERSIST(object, size) immer::FlushOps::persist_range(object, size, false); 
#define NVM_PERSIST_NOW(object, size) immer::FlushOps::persist_range_now(object, size); 
#define NVM_SFENCE() immer::FlushOps::persist_fence();
#define NVM_FINALIZE() immer::FlushOps::persist_range(0, 0, true); 
#else
#define NVM_PERSIST(object, size) do {} while(0); 
#define NVM_PERSIST_NOW(object, size) do {} while(0); 
#define NVM_SFENCE() do {} while (0);
#define NVM_FINALIZE() do {} while(0);
#endif

#if IMMER_DEBUG
#define NOTE(...) {fprintf(stderr, __VA_ARGS__);}
#define LOG(M, ...) fprintf(stderr, "%s (%s:%d) " M "\n", __func__, __FILE__, __LINE__, __VA_ARGS__);
#else
#define NOTE(M, ...) do {} while(0)
#define LOG(M, ...) do {} while(0)
#endif

// We need this to handle nvm_persist() which uses a void ptr
// so we cannot handle pointer arithmentic correctly.
// Remember int* + 4 ==> 4*sizeof(int)
#define SIZE(ptr, len) (ptr+len) - ptr;

} // namespace immer

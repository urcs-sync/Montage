/*
 * Copyright (C) 2019 University of Rochester. All rights reserved.
 * Licenced under the MIT licence. See LICENSE file in the project root for
 * details. 
 */

#ifndef _RALLOC_HPP_
#define _RALLOC_HPP_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <cstring>
#ifdef __cplusplus

#include "RegionManager.hpp"
#include "BaseMeta.hpp"
#include "SizeClass.hpp"
#include "TCache.hpp"

class Ralloc{
    friend class BaseMeta;
private:
    bool initialized;
    BaseMeta* base_md;
    Regions* _rgs;
    TCaches* t_caches;
    bool restart;
    int thd_num;


    // static SizeClass sizeclass;
    static thread_local int tid;
    inline void flush_caches(){
        for(int thd=0;thd<thd_num;thd++){
            for(int i=1;i<MAX_SZ_IDX;i++){// sc 0 is reserved.
                base_md->flush_cache(i, &t_caches[thd].t_cache[i]);
            }
        }
    }
public:
    Ralloc(int thd_num_, const char* id_, uint64_t size_ = 5*1024*1024*1024ULL);
    ~Ralloc();

    inline void* allocate(size_t sz, int tid_=tid){
        assert(initialized&&"Ralloc isn't initialized!");
        assert(tid_!=-1 && tid_<thd_num && "tid out of range!");
        return base_md->do_malloc(sz,t_caches[tid_]);
    }
    inline void* allocate(size_t num, size_t size, int tid_=tid){
        void* ptr = allocate(num*size,tid_);
        if(UNLIKELY(ptr == nullptr)) return nullptr;
        size_t real_size = malloc_size(ptr);
        memset(ptr, 0, real_size);
        FLUSH(ptr);
        FLUSHFENCE;
        return ptr;
    }
    inline void deallocate(void* ptr, int tid_=tid){
        assert(initialized&&"Ralloc isn't initialized!");
        assert(tid_!=-1 && tid_<thd_num && "tid out of range!");
        base_md->do_free(ptr,t_caches[tid_]);
    }
    void* reallocate(void* ptr, size_t new_size, int tid_=tid);

    inline void* set_root(void* ptr, uint64_t i){
        assert(initialized&&"Ralloc isn't initialized!");
        return base_md->set_root(ptr,i);
    }
    template <class T>
    inline T* get_root(uint64_t i){
        assert(initialized&&"Ralloc isn't initialized!");
        return base_md->get_root<T>(i);
    }
    inline bool is_restart(){
        return restart;
    }
    std::vector<InuseRecovery::iterator> recover(int thd = 1);

    inline void simulate_crash(){
        // Wentao: directly call destructors from main thread to mimic
        // a crash
        assert(tid==0 && "simulate_crash can only be called by main thread!");
        flush_caches();
        for(int i=0;i<thd_num;i++){
            new (&(t_caches[i])) TCaches();
        }
        base_md->fake_dirty = true;
    }
    inline size_t malloc_size(void* ptr){
        assert(in_range(ptr));
        const Descriptor* desc = base_md->desc_lookup(ptr);
        return (size_t)desc->block_size;
    }

    /* return 1 if ptr is in range of Ralloc heap, otherwise 0. */
    inline int in_range(void* ptr){
        if(_rgs->in_range(SB_IDX,ptr)) return 1;
        else return 0;
    }
    /* return 1 if the query is invalid, otherwise 0 and write start and end addr to the parameter. */
    inline int region_range(int idx, void** start_addr, void** end_addr){
        if(start_addr == nullptr || end_addr == nullptr || idx>=_rgs->cur_idx){
            return 1;
        }
        *start_addr = (void*)_rgs->regions_address[idx];
        *end_addr = (void*) ((uint64_t)_rgs->regions_address[idx] + _rgs->regions[idx]->FILESIZE);
        return 0;
    }

    inline bool is_initialized(){
        return initialized;
    }

    static void set_tid(int tid_){
        // Wentao: we deliberately allow tid to be set more than once
        // assert((tid==-1 || tid==0) && "tid set more than once!");
        // assert(tid_<thd_num && "tid exceeds total thread number passed to Ralloc constructor!");
        tid = tid_;
    }
};

// RAII ralloc global instance holder for global API
struct RallocHolder{
    Ralloc* ralloc_instance;
    int init(int thd_num, const char* _id, uint64_t size);
    ~RallocHolder(){ 
        delete ralloc_instance;
    }
};

extern RallocHolder _holder;

/* return 1 if it's a restart, otherwise 0. */
extern "C" int RP_init(const char* _id, uint64_t size = 5*1024*1024*1024ULL, int thd_num = 100);

template<class T>
T* RP_get_root(uint64_t i){
    return _holder.ralloc_instance->get_root<T>(i);
}

std::vector<InuseRecovery::iterator> RP_recover(int n = 1);
extern "C"{
#else /* __cplusplus ends */
// This is a version for pure c only
void* RP_get_root_c(uint64_t i);
/* return 1 if it's a restart, otherwise 0. */
int RP_init(const char* _id, uint64_t size, int thd_num);
/* return 1 if it's dirty, otherwise 0. */
int RP_recover_c();

#endif

void RP_close();
void RP_set_tid(int tid);
void RP_simulate_crash();
void* RP_malloc(size_t sz);
void RP_free(void* ptr);
void* RP_set_root(void* ptr, uint64_t i);
size_t RP_malloc_size(void* ptr);
void* RP_calloc(size_t num, size_t size);
void* RP_realloc(void* ptr, size_t new_size);
/* return 1 if ptr is in range of Ralloc heap, otherwise 0. */
int RP_in_prange(void* ptr);
/* return 1 if the query is invalid, otherwise 0 and write start and end addr to the parameter. */
int RP_region_range(int idx, void** start_addr, void** end_addr);
#ifdef __cplusplus
}
#endif

// #define RP_pthread_create(thd, attr, f, arg) pm_thread_create(thd, attr, f, arg)
/*
 ************class ralloc************
 * This is a persistent lock-free allocator based on LRMalloc.
 *
 * Function:
 * 		_init(string id, uint64_t thd_num):
 * 			Construct the singleton with id to decide where the data 
 * 			maps to. If the file exists, it tries to restart; otherwise,
 * 			it starts from scratch.
 * 		_close():
 * 			Shutdown the allocator by cleaning up free list 
 * 			and RegionManager pointer, but BaseMeta data will
 * 			preserve for remapping during restart.
 * 		T* _p_malloc<T>(size_t sz):
 * 			Malloc a block with size sz and type T.
 * 			Currently it only supports small allocation (<=MAX_SMALLSIZE).
 * 			If T is not void, sz will be ignored.
 * 		void _p_free(void* ptr):
 * 			Free the block pointed by ptr.
 * 		void* _set_root(void* ptr, uint64_t i):
 * 			Set i-th root to ptr, and return old i-th root if any.
 * 		void* _get_root(uint64_t i):
 * 			Return i-th root.
 *
 * Note: Main data is stored in *base_md and is mapped to 
 * filepath, which is $(HEAPFILE_PREFIX)$(id).


 * It's from paper: 
 * 		LRMalloc: A Modern and Competitive Lock-Free Dynamic Memory Allocator
 * by 
 * 		Ricardo Leite and Ricardo Rocha
 *
 * p_malloc() and p_free() have large portion of code from the open source
 * project https://github.com/ricleite/lrmalloc.
 *
 * Adapted and reimplemented by:
 * 		Wentao Cai (wcai6@cs.rochester.edu)
 *
 */

#endif /* _RALLOC_HPP_ */

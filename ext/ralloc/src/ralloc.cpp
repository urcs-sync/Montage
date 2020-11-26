/*
 * Copyright (C) 2019 University of Rochester. All rights reserved.
 * Licenced under the MIT licence. See LICENSE file in the project root for
 * details. 
 */

#include "ralloc.hpp"

#include <string>
#include <functional>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstring>

#include "pm_config.hpp"
#include "RegionManager.hpp"
#include "SizeClass.hpp"
#include "TCache.hpp"
#include "BaseMeta.hpp"

using namespace std;

thread_local int Ralloc::tid = -1;

Ralloc::Ralloc(int thd_num_, const char* id_, uint64_t size_){
    string filepath;
    string id(id_);
    thd_num = thd_num_;
    t_caches = new TCaches[thd_num];

    // // reinitialize global variables in case they haven't
    new (&ralloc::sizeclass) SizeClass();

    filepath = HEAPFILE_PREFIX + id;
    assert(sizeof(Descriptor) == DESCSIZE); // check desc size
    assert(size_ < MAX_SB_REGION_SIZE && size_ >= MIN_SB_REGION_SIZE); // ensure user input is >=MAX_SB_REGION_SIZE
    uint64_t num_sb = size_/SBSIZE;
    restart = Regions::exists_test(filepath+"_basemd");
    _rgs = new Regions();
    for(int i=0; i<LAST_IDX;i++){
    switch(i){
    case DESC_IDX:
        _rgs->create(filepath+"_desc", num_sb*DESCSIZE, true, true);
        break;
    case SB_IDX:
        _rgs->create(filepath+"_sb", num_sb*SBSIZE, true, false);
        break;
    case META_IDX:
        base_md = _rgs->create_for<BaseMeta>(filepath+"_basemd", sizeof(BaseMeta), true);
        base_md->transient_reset(_rgs, thd_num);
        break;
    } // switch
    }
    initialized = true;
    // return (int)restart;
}

Ralloc::~Ralloc(){
    if(initialized){
        // #ifndef MEM_CONSUME_TEST
        // flush_region would affect the memory consumption result (rss) and 
        // thus is disabled for benchmark testing. To enable, simply comment out
        // -DMEM_CONSUME_TEST flag in Makefile.
        flush_caches();
        delete t_caches;
        _rgs->flush_region(DESC_IDX);
        _rgs->flush_region(SB_IDX);
        // #endif
        base_md->writeback();
        initialized = false;
        delete _rgs;
    }
}

std::vector<InuseRecovery::iterator> Ralloc::recover(int thd){
    bool dirty = base_md->is_dirty();
    if(dirty) {
        // initialize transient sb free and partial lists
        base_md->avail_sb.off.store(nullptr); // initialize avail_sb
        for(int i = 0; i< MAX_SZ_IDX; i++) {
            // initialize partial list of each heap
            base_md->heaps[i].partial_list.off.store(nullptr);
        }
    }
    std::vector<InuseRecovery::iterator> ret;
    ret.reserve(thd);
    size_t begin_idx = 1;
    auto last_ptr = _rgs->regions[SB_IDX]->curr_addr_ptr->load();
    const size_t last_idx = (((uint64_t)last_ptr)>>SB_SHIFT) - 
        (((uint64_t)_rgs->lookup(SB_IDX))>>SB_SHIFT); // last sb+1
    size_t total_sb = last_idx-begin_idx;
    size_t sb_stride = total_sb/thd;
    size_t end_idx = begin_idx+sb_stride;
    for(int i=0;i<thd;i++){
        ret.emplace_back(base_md, dirty,begin_idx,end_idx);
        begin_idx+=sb_stride;
        end_idx = i==thd-2 ? last_idx : (end_idx+sb_stride);
    }
    return ret;
}

void* Ralloc::reallocate(void* ptr, size_t new_size, int tid_){
    if(ptr == nullptr) return allocate(new_size);
    if(!_rgs->in_range(SB_IDX, ptr)) return nullptr;
    size_t old_size = malloc_size(ptr);
    if(old_size == new_size) {
        return ptr;
    }
    void* new_ptr = allocate(new_size,tid_);
    if(UNLIKELY(new_ptr == nullptr)) return nullptr;
    memcpy(new_ptr, ptr, old_size);
    FLUSH(new_ptr);
    FLUSHFENCE;
    deallocate(ptr,tid_);
    return new_ptr;
}

int RallocHolder::init(int thd_num, const char* _id, uint64_t size){
    ralloc_instance = new Ralloc(thd_num, _id,size);
    ralloc_instance->set_tid(0);// set tid for main thread
    return (int)ralloc_instance->is_restart();
}

RallocHolder _holder;
/* 
 * mmap the existing heap file corresponding to id. aka restart,
 * 		and if multiple heaps exist, print out and let user select;
 * if such a heap doesn't exist, create one. aka start.
 * id is the distinguishable identity of applications.
 */
int RP_init(const char* _id, uint64_t size, int thd_num){
    return _holder.init(thd_num, _id,size);
}

std::vector<InuseRecovery::iterator> RP_recover(int n){
    return _holder.ralloc_instance->recover(n);
}

// we assume RP_close is called by the last exiting thread.
void RP_close(){
    // Wentao: this is a noop as the real function body is now in ~RallocHolder
}

void RP_set_tid(int tid){
    Ralloc::set_tid(tid);
}

void RP_simulate_crash(){
    _holder.ralloc_instance->simulate_crash();
}

void* RP_malloc(size_t sz){
    return _holder.ralloc_instance->allocate(sz);
}

void RP_free(void* ptr){
    _holder.ralloc_instance->deallocate(ptr);
}

void* RP_set_root(void* ptr, uint64_t i){
    return _holder.ralloc_instance->set_root(ptr,i);
}
void* RP_get_root_c(uint64_t i){
    return _holder.ralloc_instance->get_root<char>(i);
}

// return the size of ptr in byte.
// No check for whether ptr is allocated or isn't null
size_t RP_malloc_size(void* ptr){
    return _holder.ralloc_instance->malloc_size(ptr);
}

void* RP_realloc(void* ptr, size_t new_size){
    return _holder.ralloc_instance->reallocate(ptr,new_size);
}

void* RP_calloc(size_t num, size_t size){
    return _holder.ralloc_instance->allocate(num,size);
}

int RP_in_prange(void* ptr){
    return _holder.ralloc_instance->in_range(ptr);
}

int RP_region_range(int idx, void** start_addr, void** end_addr){
    return _holder.ralloc_instance->region_range(idx, start_addr, end_addr);
}

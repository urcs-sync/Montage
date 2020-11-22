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

#include "RegionManager.hpp"
#include "BaseMeta.hpp"
#include "SizeClass.hpp"
#include "pm_config.hpp"
#include "TCache.hpp"

using namespace std;

// namespace ralloc{
//     bool initialized = false;
//     /* persistent metadata and their layout */
//     BaseMeta* base_md;
//     Regions* _rgs;
//     std::function<void(const CrossPtr<char, SB_IDX>&, GarbageCollection&)> roots_filter_func[MAX_ROOTS];
//     extern SizeClass sizeclass;
// };
// using namespace ralloc;

std::atomic<uint64_t> Ralloc::thd_cnt(0);
SizeClass Ralloc::sizeclass();
thread_local int Ralloc::tid = -1;

Ralloc::Ralloc(const char* id_, uint64_t size_, int thd_num_){
    assert(thd_cnt.load()<=1 && "Instantiating Ralloc after spawning threads is forbidden!");
    string filepath;
    string id(id_);
    thd_num = thd_num_;
    t_caches = new TCaches[thd_num];

    // // reinitialize global variables in case they haven't
    // new (&sizeclass) SizeClass();

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
        base_md->init();
        break;
    } // switch
    }
    instance_idx=total_instance.fetch_add(1);
    t_caches.emplace_back();
    initialized = true;
    instances.push_back(this);
    // return (int)restart;
}

Ralloc::~Ralloc(){
    if(initialized){
        // #ifndef MEM_CONSUME_TEST
        // flush_region would affect the memory consumption result (rss) and 
        // thus is disabled for benchmark testing. To enable, simply comment out
        // -DMEM_CONSUME_TEST flag in Makefile.
        for(int thd=0;thd<thd_num;thd++){
            for(int i=1;i<MAX_SZ_IDX;i++){// sc 0 is reserved.
                base_md->flush_cache(i, &t_caches[thd].t_cache[i]);
            }
        }
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
    ret.reserve(n);
    size_t begin_idx = 1;
    auto last_ptr = _rgs->regions[SB_IDX]->curr_addr_ptr->load();
    const size_t last_idx = (((uint64_t)last_ptr)>>SB_SHIFT) - 
        (((uint64_t)_rgs->lookup(SB_IDX))>>SB_SHIFT); // last sb+1
    size_t total_sb = last_idx-begin_idx;
    size_t sb_stride = total_sb/n;
    size_t end_idx = begin_idx+sb_stride;
    for(int i=0;i<n;i++){
        ret.emplace_back(dirty,begin_idx,end_idx);
        begin_idx+=sb_stride;
        end_idx = i==n-2 ? last_idx : (end_idx+sb_stride);
    }
    return ret;
}

void* allocate(size_t num, size_t size){
    void* ptr = allocate(num*size);
    if(UNLIKELY(ptr == nullptr)) return nullptr;
    size_t real_size = malloc_size(ptr);
    memset(ptr, 0, real_size);
    FLUSH(ptr);
    FLUSHFENCE;
    return ptr;
}

void* Ralloc::reallocate(void* ptr, size_t new_size){
    if(ptr == nullptr) return allocate(new_size);
    if(!_rgs->in_range(SB_IDX, ptr)) return nullptr;
    size_t old_size = RP_malloc_size(ptr);
    if(old_size == new_size) {
        return ptr;
    }
    void* new_ptr = allocate(new_size);
    if(UNLIKELY(new_ptr == nullptr)) return nullptr;
    memcpy(new_ptr, ptr, old_size);
    FLUSH(new_ptr);
    FLUSHFENCE;
    RP_free(ptr);
    return new_ptr;
}

struct RallocHolder{
    Ralloc* ralloc_instance;
    inline int init(const char* _id, uint64_t size) {
        ralloc_instance = new Ralloc(_id,size)
        return (int)ralloc_instance->is_restart();
    }
    ~RallocHolder(){ 
        delete ralloc_instance;
    }
};

static RallocHolder _holder;
/* 
 * mmap the existing heap file corresponding to id. aka restart,
 * 		and if multiple heaps exist, print out and let user select;
 * if such a heap doesn't exist, create one. aka start.
 * id is the distinguishable identity of applications.
 */
int RP_init(const char* _id, uint64_t size){
    return _holder.init(_id,size);
}

std::vector<InuseRecovery::iterator> RP_recover(int n){
    return _holder.ralloc_instance->recover(n);
}

// we assume RP_close is called by the last exiting thread.
void RP_close(){
    // Wentao: this is a noop as the real function body is now in ~RallocHolder
}

void RP_simulate_crash(int tid){
    _holder.ralloc_instance->simulate_crash(tid);
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

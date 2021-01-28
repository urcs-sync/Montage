#ifndef EPOCH_HPP
#define EPOCH_HPP

#include <atomic>
#include <unordered_map>
#include <set>
#include <map>
#include <thread>
#include <condition_variable>
#include <string>
#include "TestConfig.hpp"
#include "ConcurrentPrimitives.hpp"
#include "PersistFunc.hpp"
#include "HarnessUtils.hpp"
#include "Persistent.hpp"
#include "persist_utils.hpp"

#include "common_macros.hpp"
#include "TransactionTrackers.hpp"
#include "PerThreadContainers.hpp"
#include "ToBePersistedContainers.hpp"
#include "ToBeFreedContainers.hpp"
#include "EpochAdvancers.hpp"

class Recoverable;

namespace pds{

struct OldSeeNewException : public std::exception {
    const char * what () const throw () {
        return "OldSeeNewException not handled.";
    }
};

enum PBlkType {INIT, ALLOC, UPDATE, DELETE, RECLAIMED, EPOCH, OWNED};

class EpochSys;

/////////////////////////////
// PBlk-related structures //
/////////////////////////////

class PBlk{
    friend class EpochSys;
    friend class Recoverable;
protected:
    // Wentao: the first word should NOT be any persistent value for
    // epoch-system-level recovery (i.e., epoch), as Ralloc repurposes the first
    // word for block free list, which may interfere with the recovery.
    // Currently we use (transient) "reserved" as the first word. If we decide to
    // remove this field, we need to either prepend another dummy word, or
    // change the block free list in Ralloc.

    // transient.
    void* _reserved;

    uint64_t epoch = NULL_EPOCH;
    PBlkType blktype = INIT;
    uint64_t owner_id = 0; // TODO: make consider abandon this field and use id all the time.
    uint64_t id = 0;
    pptr<PBlk> retire = nullptr;
    // bool persisted = false; // For debug purposes. Might not be needed at the end. 

    // void call_persist(){ // For debug purposes. Might not be needed at the end. 
    //     persist();
    //     persisted = true;
    // }
public:
    void set_epoch(uint64_t e){
        // only for testing
        epoch=e;
    }
    uint64_t get_epoch(){
        return epoch;
    }
    // id gets inited by EpochSys instance.
    PBlk(): epoch(NULL_EPOCH), blktype(INIT), owner_id(0), retire(nullptr){}
    // id gets inited by EpochSys instance.
    PBlk(const PBlk* owner):
        blktype(OWNED), owner_id(owner->blktype==OWNED? owner->owner_id : owner->id) {}
    PBlk(const PBlk& oth): blktype(oth.blktype==OWNED? OWNED:INIT), owner_id(oth.owner_id), id(oth.id) {}
    inline uint64_t get_id() {return id;}
    virtual pptr<PBlk> get_data() {return nullptr;}
    virtual ~PBlk(){
        // Wentao: we need to zeroize epoch and flush it, avoiding it left after free
        epoch = NULL_EPOCH;
        // persist_func::clwb(&epoch);
    }
};

template<typename T>
class PBlkArray : public PBlk{
    friend class EpochSys;
    size_t size;
    // NOTE: see EpochSys::alloc_pblk_array() for its sementical allocators.
    PBlkArray(): PBlk(){}
    PBlkArray(PBlk* owner) : PBlk(owner), content((T*)((char*)this + sizeof(PBlkArray<T>))){}
public:
    PBlkArray(const PBlkArray<T>& oth): PBlk(oth), size(oth.size),
        content((T*)((char*)this + sizeof(PBlkArray<T>))){}
    virtual ~PBlkArray(){};
    T* content; //transient ptr
    inline size_t get_size()const{return size;}
};

struct Epoch : public PBlk{
    std::atomic<uint64_t> global_epoch;
    void persist(){}
    Epoch(){
        global_epoch.store(NULL_EPOCH, std::memory_order_relaxed);
    }
};

//////////////////
// Epoch System //
//////////////////

enum SysMode {ONLINE, RECOVER};

struct sc_desc_t;

class EpochSys{
private:
    // persistent fields:
    Epoch* epoch_container = nullptr;
    std::atomic<uint64_t>* global_epoch = nullptr;

    // semi-persistent fields:
    // TODO: set a periodic-updated persistent boundary to recover to.
    UIDGenerator uid_generator;

    // transient fields:
    TransactionTracker* trans_tracker = nullptr;
    ToBePersistContainer* to_be_persisted = nullptr;
    ToBeFreedContainer* to_be_freed = nullptr;
    EpochAdvancer* epoch_advancer = nullptr;

    GlobalTestConfig* gtc = nullptr;
    Ralloc* _ral = nullptr;
    int task_num;
    static std::atomic<int> esys_num;

public:

    /* static */
    static thread_local int tid;
    
    // system mode that toggles on/off PDELETE for recovery purpose.
    SysMode sys_mode = ONLINE;

    EpochSys(GlobalTestConfig* _gtc) : uid_generator(_gtc->task_num), gtc(_gtc) {
        std::string heap_name = get_ralloc_heap_name();
        // task_num+1 to construct Ralloc for dedicated epoch advancer
        _ral = new Ralloc(_gtc->task_num+1,heap_name.c_str(),REGION_SIZE);
        reset(); // TODO: change to recover() later on.
    }

    void flush(){
        for (int i = 0; i < 2; i++){
            sync(NULL_EPOCH);
        }
    }

    ~EpochSys(){
        // std::cout<<"epochsys descructor called"<<std::endl;
        trans_tracker->finalize();
        // flush(); // flush is done in epoch_advancer's destructor.
        if (epoch_advancer){
            delete epoch_advancer;
        }
        if (gtc->verbose){
            std::cout<<"final epoch:"<<global_epoch->load()<<std::endl;
        }
        delete trans_tracker;
        delete to_be_persisted;
        delete to_be_freed;
        delete _ral;
    }

    void parse_env();

    std::string get_ralloc_heap_name(){
        if (!gtc->checkEnv("HeapName")){
            int esys_id = esys_num.fetch_add(1);
            assert(esys_id<=0xfffff);
            char* heap_prefix = (char*) malloc(L_cuserid+10);
            cuserid(heap_prefix);
            char* heap_suffix = (char*) malloc(12);
            sprintf(heap_suffix, "_mon_%06X", esys_id);
            strcat(heap_prefix, heap_suffix);
            gtc->setEnv("HeapName", std::string(heap_prefix));
        }
        std::string ret;
        if (gtc->checkEnv("HeapName")){
            ret = gtc->getEnv("HeapName");
        }
        return ret;
    }

    void reset(){
        task_num = gtc->task_num;
        if (!epoch_container){
            epoch_container = new_pblk<Epoch>();
            epoch_container->blktype = EPOCH;
            global_epoch = &epoch_container->global_epoch;
        }
        global_epoch->store(INIT_EPOCH, std::memory_order_relaxed);
        parse_env();
    }

    void simulate_crash(){
        assert(tid==0 && "simulate_crash can only be called by main thread");
        // if(tid==0){
            delete epoch_advancer;
            epoch_advancer = nullptr;
        // }
        _ral->simulate_crash();
    }

    ////////////////
    // Operations //
    ////////////////

    static void init_thread(int _tid){
        EpochSys::tid = _tid;
        Ralloc::set_tid(_tid);
    }

    void* malloc_pblk(size_t sz){
        return _ral->allocate(sz);
    }

    // allocate a T-typed block on Ralloc and
    // construct using placement new
    template <class T, typename... Types>
    T* new_pblk(Types... args){
        T* ret = (T*)_ral->allocate(sizeof(T));
        new (ret) T (args...);
        return ret;
    }

    // deallocate pblk, giving it back to Ralloc
    template <class T>
    void delete_pblk(T* pblk){
        pblk->~T();
        _ral->deallocate(pblk);
    }

    // check if global is the same as c.
    bool check_epoch(uint64_t c);

    // start transaction in the current epoch c.
    // prevent current epoch advance from c+1 to c+2.
    uint64_t begin_transaction();
 
    // end transaction, release the holding of epoch increments.
    void end_transaction(uint64_t c);

    // end read only transaction, release the holding of epoch increments.
    void end_readonly_transaction(uint64_t c);

    // abort transaction, release the holding of epoch increments without other traces.
    void abort_transaction(uint64_t c);

    // validate an access in epoch c. throw exception if last update is newer than c.
    void validate_access(const PBlk* b, uint64_t c);

    // register the allocation of a PBlk during a transaction.
    // called for new blocks at both pnew (holding them in
    // pending_allocs) and begin_op (registering them with the
    // acquired epoch).
    template<typename T>
    T* register_alloc_pblk(T* b, uint64_t c);

    template<typename T>
    T* reset_alloc_pblk(T* b);

    template<typename T>
    PBlkArray<T>* alloc_pblk_array(size_t s, uint64_t c);

    template<typename T>
    PBlkArray<T>* alloc_pblk_array(PBlk* owenr, size_t s, uint64_t c);

    template<typename T>
    PBlkArray<T>* copy_pblk_array(const PBlkArray<T>* oth, uint64_t c);

    // register update of a PBlk during a transaction.
    // called by the API.
    void register_update_pblk(PBlk* b, uint64_t c);

    // free a PBlk during a transaction.
    template<typename T>
    void free_pblk(T* b, uint64_t c);

    // retire a PBlk during a transaction.
    template<typename T>
    void retire_pblk(T* b, uint64_t c);

    // reclaim a retired PBlk.
    template<typename T>
    void reclaim_pblk(T* b, uint64_t c);

    // read a PBlk during a transaction.
    template<typename T>
    const T* openread_pblk(const T* b, uint64_t c);

    // read a PBlk during a transaction, without ever throwing OldSeeNew.
    template<typename T>
    const T* openread_pblk_unsafe(const T* b, uint64_t c);

    // get a writable copy of a PBlk.
    template<typename T>
    T* openwrite_pblk(T* b, uint64_t c);

    // block, call for persistence of epoch c, and wait until finish.
    void sync(uint64_t c){
        epoch_advancer->sync(c);
    }


    /////////////////
    // Bookkeeping //
    /////////////////

    // get the current global epoch number.
    uint64_t get_epoch();

    // try to advance global epoch, helping others along the way.
    void advance_epoch(uint64_t c);

    // a version of advance_epoch for a SINGLE bookkeeping thread.
    void advance_epoch_dedicated();

    /////////////
    // Recover //
    /////////////
    
    // recover all PBlk decendants. return an iterator.
    std::unordered_map<uint64_t, PBlk*>* recover(const int rec_thd = 2);
};


template<typename T>
T* EpochSys::register_alloc_pblk(T* b, uint64_t c){
    // static_assert(std::is_convertible<T*, PBlk*>::value,
    //     "T must inherit PBlk as public");
    // static_assert(std::is_copy_constructible<T>::value,
    //             "requires copying");
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);
        
    PBlk* blk = b;
    assert(c != NULL_EPOCH);
    blk->epoch = c;
    assert(blk->blktype == INIT || blk->blktype == OWNED); 
    if (blk->blktype == INIT){
        blk->blktype = ALLOC;
    }
    if (blk->id == 0){
        blk->id = uid_generator.get_id(tid);
    }

    to_be_persisted->register_persist(blk, _ral->malloc_size(blk), c);
    PBlk* data = blk->get_data();
    if (data){
        register_alloc_pblk(data, c);
    }
    return b;
}

template<typename T>
T* EpochSys::reset_alloc_pblk(T* b){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);
    PBlk* blk = b;
    blk->epoch = NULL_EPOCH;
    assert(blk->blktype == ALLOC); 
    blk->blktype = INIT;
    PBlk* data = blk->get_data();
    if (data){
        reset_alloc_pblk(data);
    }
    return b;
}


template<typename T>
PBlkArray<T>* EpochSys::alloc_pblk_array(size_t s, uint64_t c){
    PBlkArray<T>* ret = static_cast<PBlkArray<T>*>(
        _ral->allocate(sizeof(PBlkArray<T>) + s*sizeof(T)));
    new (ret) PBlkArray<T>();
    // Wentao: content initialization has been moved into PBlkArray constructor
    ret->size = s;
    T* p = ret->content;
    for (int i = 0; i < s; i++){
        new (p) T();
        p++;
    }
    register_alloc_pblk(ret);
    // temporarily removed the following persist:
    // we have to persist it after modifications anyways.
    // to_be_persisted->register_persist(ret, _ral->malloc_size(ret), c);
    return ret;
}

template<typename T>
PBlkArray<T>* EpochSys::alloc_pblk_array(PBlk* owner, size_t s, uint64_t c){
    PBlkArray<T>* ret = static_cast<PBlkArray<T>*>(
        _ral->allocate(sizeof(PBlkArray<T>) + s*sizeof(T)));
    new (ret) PBlkArray<T>(owner);
    ret->size = s;
    T* p = ret->content;
    for (size_t i = 0; i < s; i++){
        new (p) T();
        p++;
    }
    register_alloc_pblk(ret);
    // temporarily removed the following persist:
    // we have to persist it after modifications anyways.
    // to_be_persisted->register_persist(ret, _ral->malloc_size(ret), c);
    return ret;
}

template<typename T>
PBlkArray<T>* EpochSys::copy_pblk_array(const PBlkArray<T>* oth, uint64_t c){
    PBlkArray<T>* ret = static_cast<PBlkArray<T>*>(
        _ral->allocate(sizeof(PBlkArray<T>) + oth->size*sizeof(T)));
    new (ret) PBlkArray<T>(*oth);
    memcpy(ret->content, oth->content, oth->size*sizeof(T));
    ret->epoch = c;
    to_be_persisted->register_persist(ret, _ral->malloc_size(ret), c);
    return ret;
}

template<typename T>
void EpochSys::free_pblk(T* b, uint64_t c){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);
    
    PBlk* blk = b;
    uint64_t e = blk->epoch;
    PBlkType blktype = blk->blktype;
    if (e > c){
        throw OldSeeNewException();
    } else if (e == c){
        if (blktype == ALLOC){
            to_be_persisted->register_persist_raw(blk, c);
            _ral->deallocate(b);
            return;
        } else if (blktype == UPDATE){
            blk->blktype = DELETE;
        } else if (blktype == DELETE) {
            errexit("double free error.");
        }
    } else {
        // NOTE: The deletion node will be essentially "leaked" during online phase,
        // which may fundamentally confuse valgrind.
        // Consider keeping reference of all delete node for debugging purposes.
        PBlk* del = new_pblk<PBlk>(*blk);
        del->blktype = DELETE;
        del->epoch = c;
        // to_be_persisted[c%4].push(del);
        to_be_persisted->register_persist(del, _ral->malloc_size(del), c);
        // to_be_freed[(c+1)%4].push(del);
        to_be_freed->register_free(del, c+1);
    }
    // to_be_freed[c%4].push(b);
    to_be_freed->register_free(b, c);
}

template<typename T>
void EpochSys::retire_pblk(T* b, uint64_t c){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);

    PBlk* blk = b;
    if (blk->retire != nullptr){
        errexit("double retire error, or this block was tentatively retired before recent crash.");
    }
    uint64_t e = blk->epoch;
    PBlkType blktype = blk->blktype;
    if (e > c){
        throw OldSeeNewException();
    } else if (e == c){
        // retiring a block updated/allocated in the same epoch.
        // changing it directly to a DELETE node without putting it in to_be_freed list.
        if (blktype == ALLOC || blktype == UPDATE){
            blk->blktype = DELETE;
        } else {
            errexit("wrong type of PBlk to retire.");
        }
    } else {
        // note this actually modifies 'retire' field of a PBlk from the past
        // Which is OK since nobody else will look at this field.
        blk->retire = new_pblk<PBlk>(*b);
        blk->retire->blktype = DELETE;
        blk->retire->epoch = c;
        to_be_persisted->register_persist(blk->retire, _ral->malloc_size(blk->retire), c);
    }
    to_be_persisted->register_persist(b, _ral->malloc_size(b), c);
    
}

template<typename T>
void EpochSys::reclaim_pblk(T* b, uint64_t c){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);

    PBlk* blk = b;
    uint64_t e = blk->epoch;
    if (e > c){
        errexit("reclaiming a block created in a newer epoch");
    }
    if (blk->retire == nullptr){
        if (blk->blktype != DELETE){
            // errexit("reclaiming an unretired PBlk.");
            // this PBlk is not retired. we PDELETE it here.
            free_pblk(b, c);
        } else if (e < c-1){ // this block was retired at least two epochs ago.
            _ral->deallocate(b);
        } else {// this block was retired less than two epochs ago.
            // NOTE: putting b in c's to-be-free is safe, but not e's,
            // since if e==c-1, global epoch may go to c+1 and c-1's to-be-freed list
            // may be considered cleaned before we putting b in it.
            to_be_freed->register_free(blk, c);
        }
    } else {
        uint64_t e_retire = blk->retire->epoch;
        if (e_retire > c){
            errexit("reclaiming a block retired in a newer epoch");
        }
        if (e < c-1){
            // this block was retired at least two epochs ago.
            // Note that reclamation of retire node need to be deferred after a fence.
            to_be_freed->register_free(blk->retire, c);
            _ral->deallocate(b);
        } else {
            // retired in recent epoch, act like a free_pblk.
            to_be_freed->register_free(blk->retire, c+1);
            to_be_freed->register_free(b, c);
        }
    }
}

template<typename T>
const T* EpochSys::openread_pblk(const T* b, uint64_t c){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);

    validate_access(b, c);
    return openread_pblk_unsafe(b, c);
}

template<typename T>
const T* EpochSys::openread_pblk_unsafe(const T* b, uint64_t c){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);

    return b;
}

template<typename T>
T* EpochSys::openwrite_pblk(T* b, uint64_t c){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);
    
    validate_access(b, c);
    PBlk* blk = b;
    if (blk->epoch < c){
        // to_be_freed[c%4].push(b);
        to_be_freed->register_free(b, c);
        b = new_pblk<T>(*b);
        PBlk* blk = b;
        assert(blk);
        blk->epoch = c;
        blk->blktype = UPDATE;
    }
    // cannot put b in to-be-persisted list here (only) before the actual modification,
    // because help() may grab and flush it before the modification. This is currently
    // done by the API module.
    return b;
}




}

#endif
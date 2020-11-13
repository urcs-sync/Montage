#ifndef EPOCH_HPP
#define EPOCH_HPP

#include <atomic>
#include <unordered_map>
#include <set>
#include <map>
#include <thread>
#include <condition_variable>
#include "TestConfig.hpp"
#include "ConcurrentPrimitives.hpp"
#include "PersistFunc.hpp"
#include "HarnessUtils.hpp"
#include "Persistent.hpp"
#include "persist_utils.hpp"

#include "common_macros.hpp"
#include "PersistStructs.hpp"
#include "TransactionTrackers.hpp"
#include "PerThreadContainers.hpp"
#include "ToBePersistedContainers.hpp"
#include "ToBeFreedContainers.hpp"
#include "EpochAdvancers.hpp"

namespace pds{

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
    int task_num;

public:

    /* static */
    static thread_local int tid;
    
    std::mutex dedicated_epoch_advancer_lock;

    /* public members for API */
    // current epoch of each thread.
    padded<uint64_t>* epochs = nullptr;
    // local descriptors for DCSS
    // TODO: maybe put this into a derived class for NB data structures?
    padded<sc_desc_t>* local_descs = nullptr;
    // containers for pending allocations
    padded<std::unordered_set<PBlk*>>* pending_allocs = nullptr;
    // system mode that toggles on/off PDELETE for recovery purpose.
    SysMode sys_mode = ONLINE;

    EpochSys(GlobalTestConfig* _gtc) : uid_generator(_gtc->task_num), gtc(_gtc) {
        epochs = new padded<uint64_t>[gtc->task_num];
        for(int i = 0; i < gtc->task_num; i++){
            epochs[i].ui = NULL_EPOCH;
        }
        local_descs = new padded<sc_desc_t>[gtc->task_num];
        pending_allocs = new padded<std::unordered_set<PBlk*>>[gtc->task_num];
        reset(); // TODO: change to recover() later on.
    }

    void flush(){
        for (int i = 0; i < 4; i++){
            advance_epoch_dedicated();
        }
    }

    ~EpochSys(){
        // std::cout<<"epochsys descructor called"<<std::endl;
        trans_tracker->finalize();
        if (epoch_advancer){
            delete epoch_advancer;
        }
        flush();
        delete trans_tracker;
        delete to_be_persisted;
        delete to_be_freed;
        delete epochs;
        delete local_descs;
    }

    void parse_env();

    void reset(){
        task_num = gtc->task_num;
        if (!epoch_container){
            epoch_container = new Epoch();
            epoch_container->blktype = EPOCH;
            global_epoch = &epoch_container->global_epoch;
        }
        global_epoch->store(INIT_EPOCH, std::memory_order_relaxed);
        parse_env();
    }

    void simulate_crash(){
        if(tid==0){
            delete epoch_advancer;
            epoch_advancer = nullptr;
        }
        Persistent::simulate_crash(tid);
    }

    /////////
    // API //
    /////////

    static void init_thread(int _tid){
        EpochSys::tid = _tid;
    }

    bool check_epoch(){
        return check_epoch(epochs[tid].ui);
    }

    void begin_op(){
        assert(epochs[tid].ui == NULL_EPOCH); 
        epochs[tid].ui = begin_transaction();
        // TODO: any room for optimization here?
        // TODO: put pending_allocs-related stuff into operations?
        for (auto b = pending_allocs[tid].ui.begin(); 
            b != pending_allocs[tid].ui.end(); b++){
            register_alloc_pblk(*b, epochs[tid].ui);
        }
    }

    void end_op(){
        if (epochs[tid].ui != NULL_EPOCH){
            end_transaction(epochs[tid].ui);
            epochs[tid].ui = NULL_EPOCH;
        }
        pending_allocs[tid].ui.clear();
    }

    void end_readonly_op(){
        if (epochs[tid].ui != NULL_EPOCH){
            end_readonly_transaction(epochs[tid].ui);
            epochs[tid].ui = NULL_EPOCH;
        }
        assert(pending_allocs[tid].ui.empty());
    }

    void abort_op(){
        assert(epochs[tid].ui != NULL_EPOCH);
        abort_transaction(epochs[tid].ui);
        epochs[tid].ui = NULL_EPOCH;
    }

    template<typename T>
    void pdelete(T* b){
        ASSERT_DERIVE(T, PBlk);
        ASSERT_COPY(T);

        if (sys_mode == ONLINE){
            if (epochs[tid].ui != NULL_EPOCH){
                free_pblk(b, epochs[tid].ui);
            } else {
                if (b->epoch == NULL_EPOCH){
                    assert(pending_allocs[tid].ui.find(b) != pending_allocs[tid].ui.end());
                    pending_allocs[tid].ui.erase(b);
                }
                delete b;
            }
        }
    }

    template<typename T>
    void pretire(T* b){
        assert(eochs[tid].ui != NULL_EPOCH);
        retire_pblk(b, epochs[tid].ui);
    }

    template<typename T>
    void preclaim(T* b){
        if (epochs[tid].ui == NULL_EPOCH){
            begin_op();
        }
        reclaim_pblk(b, epochs[tid].ui);
        if (epochs[tid].ui == NULL_EPOCH){
            end_op();
        }
    }

    template<typename T>
    T* register_alloc_pblk(T* b){
        return register_alloc_pblk(b, epochs[tid].ui);
    }

    template<typename T>
    void register_update_pblk(T* b){
        register_update_pblk(b, epochs[tid].ui);
    }

    template<typename T>
    const T* openread_pblk(const T* b){
        assert(epochs[tid].ui != NULL_EPOCH);
        return openread_pblk(b, epochs[tid].ui);
    }

    template<typename T>
    const T* openread_pblk_unsafe(const T* b){
        if (epochs[tid].ui != NULL_EPOCH){
            return openread_pblk_unsafe(b, epochs[tid].ui);
        } else {
            return b;
        }
    }

    template<typename T>
    T* openwrite_pblk(T* b){
        assert(epochs[tid].ui != NULL_EPOCH);
        return openwrite_pblk(b, epochs[tid].ui);
    }

    void recover_mode(){
        sys_mode = RECOVER; // PDELETE -> nop
    }

    void online_mode(){
        sys_mode = ONLINE;
    }

    ////////////////
    // Operations //
    ////////////////

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
    template<typename T>
    T* register_alloc_pblk(T* b, uint64_t c);

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

    /////////////////
    // Bookkeeping //
    /////////////////

    // try to advance global epoch, helping others along the way.
    void advance_epoch(uint64_t c);

    // a version of advance_epoch for a SINGLE bookkeeping thread.
    void advance_epoch_dedicated();

    // try to help with block persistence and reclamation.
    void help();

    // try to help with thread local persistence and reclamation.
    void help_local();

    /////////////
    // Recover //
    /////////////
    
    // recover all PBlk decendants. return an iterator.
    std::unordered_map<uint64_t, PBlk*>* recover(const int rec_thd);
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
    if (c == NULL_EPOCH){
        // register alloc before BEGIN_OP, put it into pending_allocs bucket and
        // return. Will be done by the BEGIN_OP that calls this again with a
        // non-NULL c.
        pending_allocs[tid].ui.insert(blk);
        return b;
    }
    blk->epoch = c;
    // Wentao: It's possible that payload is registered multiple times
    assert(blk->blktype == INIT || blk->blktype == OWNED || 
           blk->blktype == ALLOC); 
    if (blk->blktype == INIT){
        blk->blktype = ALLOC;
    }
    if (blk->id == 0){
        blk->id = uid_generator.get_id(tid);
    }

    to_be_persisted->register_persist(blk, c);
    PBlk* data = blk->get_data();
    if (data){
        register_alloc_pblk(data, c);
    }
    return b;
}

template<typename T>
PBlkArray<T>* EpochSys::alloc_pblk_array(size_t s, uint64_t c){
    PBlkArray<T>* ret = static_cast<PBlkArray<T>*>(
        RP_malloc(sizeof(PBlkArray<T>) + s*sizeof(T)));
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
    // to_be_persisted->register_persist(ret, c);
    return ret;
}

template<typename T>
PBlkArray<T>* EpochSys::alloc_pblk_array(PBlk* owner, size_t s, uint64_t c){
    PBlkArray<T>* ret = static_cast<PBlkArray<T>*>(
        RP_malloc(sizeof(PBlkArray<T>) + s*sizeof(T)));
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
    // to_be_persisted->register_persist(ret, c);
    return ret;
}

template<typename T>
PBlkArray<T>* EpochSys::copy_pblk_array(const PBlkArray<T>* oth, uint64_t c){
    PBlkArray<T>* ret = static_cast<PBlkArray<T>*>(
        RP_malloc(sizeof(PBlkArray<T>) + oth->size*sizeof(T)));
    new (ret) PBlkArray<T>(*oth);
    memcpy(ret->content, oth->content, oth->size*sizeof(T));
    ret->epoch = c;
    to_be_persisted->register_persist(ret, c);
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
            delete(b);
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
        PBlk* del = new PBlk(*blk);
        del->blktype = DELETE;
        del->epoch = c;
        // to_be_persisted[c%4].push(del);
        to_be_persisted->register_persist(del, c);
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
        blk->retire = new PBlk(*b);
        blk->retire->blktype = DELETE;
        blk->retire->epoch = c;
        to_be_persisted->register_persist(blk->retire, c);
    }
    to_be_persisted->register_persist(b, c);
    
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
            delete(b);
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
            delete(b);
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
        b = new T(*b);
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
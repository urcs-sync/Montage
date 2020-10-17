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
// #include "EpochSysVerifyTest.hpp"

class EpochSysVerifyTest;

namespace pds{

#define ASSERT_DERIVE(der, base)\
    static_assert(std::is_convertible<der*, base*>::value,\
        #der " must inherit " #base " as public");

#define ASSERT_COPY(t)\
    static_assert(std::is_copy_constructible<t>::value,\
        "type" #t "requires copying");

#define INIT_EPOCH 3
#define NULL_EPOCH 0

extern __thread int _tid;

enum SysMode {ONLINE, RECOVER};

extern SysMode sys_mode;

struct OldSeeNewException : public std::exception {
   const char * what () const throw () {
      return "OldSeeNewException not handled.";
   }
};

class UIDGenerator{
    padded<uint64_t>* curr_ids;
public:
    void init(uint64_t task_num){
        uint64_t buf = task_num-1;
        int shift = 64;
        uint64_t max = 1;
        for (; buf != 0; buf >>= 1){
            shift--;
            max <<= 1;
        }
        curr_ids = new padded<uint64_t>[max];
        for (uint64_t i = 0; i < max; i++){
            curr_ids[i].ui = i << shift;
        }
    }
    uint64_t get_id(int tid){
        return curr_ids[tid].ui++;
    }
};

class EpochSys;

enum PBlkType {INIT, ALLOC, UPDATE, DELETE, RECLAIMED, EPOCH, OWNED};

// class PBlk{
class PBlk : public Persistent{
    friend class EpochSys;
    friend class EpochSysVerifyTest;
    static UIDGenerator uid_generator;
protected:
    // Wentao: the first word should NOT be any persistent value for
    // epoch-system-level recovery (i.e., epoch), as Ralloc repurposes the first
    // word for block free list, which may interfere with the recovery.
    // Currently we use (transient) payload as the first word. If we decide to
    // remove this field, we need to either prepend another dummy word, or
    // change the block free list in Ralloc.

    // only used in transient headers.
    PBlk* payload;

    uint64_t epoch = NULL_EPOCH;
    PBlkType blktype = INIT;
    uint64_t owner_id = 0;
    uint64_t id = 0;
    pptr<PBlk> retire = nullptr;
    // bool persisted = false; // For debug purposes. Might not be needed at the end. 

    // void call_persist(){ // For debug purposes. Might not be needed at the end. 
    //     persist();
    //     persisted = true;
    // }
public:
    static void init(int task_num){
        uid_generator.init(task_num);
    }
    // PBlk(uint64_t e): epoch(e), persisted(false){}
    PBlk(): epoch(NULL_EPOCH), blktype(INIT), owner_id(0), id(uid_generator.get_id(_tid)), retire(nullptr){}
    // PBlk(bool is_data): blktype(is_data?DATA:INIT), id(uid_generator.get_id(tid)) {}
    PBlk(const PBlk* owner): 
        blktype(OWNED), owner_id(owner->blktype==OWNED? owner->owner_id : owner->id), 
        id(uid_generator.get_id(_tid)) {}
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
// class PArray : public PBlk{
//     T* content;
//     // TODO: set constructor private. use EpochSys::alloc_array
//     // to get memory from Ralloc and in-place new PArray in the front, and
//     // in-place new all T objects in the array (with exception for 1-word T's). 
// };

#include "TransactionTrackers.hpp"
#include "PerThreadContainers.hpp"
#include "ToBePersistedContainers.hpp"
#include "ToBeFreedContainers.hpp"

class EpochSys{
    
    /////////////////////
    // Epoch Advancers //
    /////////////////////

    class EpochAdvancer{
    public:
        virtual void set_epoch_freq(int epoch_freq) = 0;
        virtual void set_help_freq(int help_freq) = 0;
        virtual void on_end_transaction(EpochSys* esys, uint64_t c) = 0;
        virtual ~EpochAdvancer(){}
    };

    class SingleThreadEpochAdvancer : public EpochAdvancer{
        // uint64_t trans_cnt;
        padded<uint64_t>* trans_cnts;
        uint64_t epoch_threshold = 0x1ULL << 19;
        uint64_t help_threshold = 0x1ULL << 6;
    public:
        SingleThreadEpochAdvancer(GlobalTestConfig* gtc){
            trans_cnts = new padded<uint64_t>[gtc->task_num];
            for (int i = 0; i < gtc->task_num; i++){
                trans_cnts[i].ui = 0;
            }
        }
        void set_epoch_freq(int epoch_power){
            epoch_threshold = 0x1ULL << epoch_power;
        }
        void set_help_freq(int help_power){
            help_threshold = 0x1ULL << help_power;
        }
        void on_end_transaction(EpochSys* esys, uint64_t c){
            assert(_tid != -1);
            trans_cnts[_tid].ui++;
            if (_tid == 0){
                // only a single thread can advance epochs.
                if (trans_cnts[_tid].ui % epoch_threshold == 0){
                    esys->advance_epoch(c);
                } 
                // else if (trans_cnts[_tid].ui % help_threshold == 0){
                //     esys->help_local();
                // }
            }
            // else {
            //     if (trans_cnts[_tid].ui % help_threshold == 0){
            //         esys->help_local();
            //     }
            // }
        }
    };

    class GlobalCounterEpochAdvancer : public EpochAdvancer{
        std::atomic<uint64_t> trans_cnt;
        uint64_t epoch_threshold = 0x1ULL << 14;
        uint64_t help_threshold = 0x1ULL;
    public:
        // GlobalCounterEpochAdvancer();
        void set_epoch_freq(int epoch_power){
            epoch_threshold = 0x1ULL << epoch_power;
        }
        void set_help_freq(int help_power){
            help_threshold = 0x1ULL << help_power;
        }
        void on_end_transaction(EpochSys* esys, uint64_t c){
            uint64_t curr_cnt = trans_cnt.fetch_add(1, std::memory_order_acq_rel);
            if (curr_cnt % epoch_threshold == 0){
                esys->advance_epoch(c);
            } else if (curr_cnt % help_threshold == 0){
                esys->help_local();
            }
        }
    };

    class DedicatedEpochAdvancer : public EpochAdvancer{
        EpochSys* esys;
        std::thread advancer_thread;
        std::atomic<bool> started;
        uint64_t epoch_length = 100*1000;
        void advancer(){
            while(!started.load()){}
            while(started.load()){
                esys->advance_epoch_dedicated();
                std::this_thread::sleep_for(std::chrono::microseconds(epoch_length));
            }
            // std::cout<<"advancer_thread terminating..."<<std::endl;
        }
    public:
        DedicatedEpochAdvancer(GlobalTestConfig* gtc, EpochSys* es):esys(es){
            if (gtc->checkEnv("EpochLength")){
                epoch_length = stoi(gtc->getEnv("EpochLength"));
            } else {
                epoch_length = 100*1000;
            }
            if (gtc->checkEnv("EpochLengthUnit")){
                std::string env_unit = gtc->getEnv("EpochLengthUnit");
                if (env_unit == "Second"){
                    epoch_length *= 1000000;
                } else if (env_unit == "Millisecond"){
                    epoch_length *= 1000;
                } else if (env_unit == "Microsecond"){
                    // do nothing.
                } else {
                    errexit("time unit not supported.");
                }
            }

            started.store(false);
            advancer_thread = std::move(std::thread(&DedicatedEpochAdvancer::advancer, this));
            started.store(true);
        }
        ~DedicatedEpochAdvancer(){
            // std::cout<<"terminating advancer_thread"<<std::endl;
            started.store(false);
            if (advancer_thread.joinable()){
                advancer_thread.join();
            }
            // std::cout<<"terminated advancer_thread"<<std::endl;
        }
        void set_epoch_freq(int epoch_interval){
        }
        void set_help_freq(int help_interval){
        }
        void on_end_transaction(EpochSys* esys, uint64_t c){
            // do nothing here.
        }
    };

    class NoEpochAdvancer : public EpochAdvancer{
        // an epoch advancer that does absolutely nothing.
    public:
        // GlobalCounterEpochAdvancer();
        void set_epoch_freq(int epoch_power){}
        void set_help_freq(int help_power){}
        void on_end_transaction(EpochSys* esys, uint64_t c){}
    };

    friend class EpochSysVerifyTest;
    
private:
    // persistent fields:
    Epoch* epoch_container = nullptr;
    std::atomic<uint64_t>* global_epoch = nullptr;

    // transient fields:
    TransactionTracker* trans_tracker = nullptr;
    ToBePersistContainer* to_be_persisted = nullptr;
    ToBeFreedContainer* to_be_freed = nullptr;
    EpochAdvancer* epoch_advancer = nullptr;

    GlobalTestConfig* gtc = nullptr;
    int task_num;
    // static __thread int tid;

    bool consistent_increment(std::atomic<uint64_t>& counter, const uint64_t c);

public:

    std::mutex dedicated_epoch_advancer_lock;

    EpochSys(GlobalTestConfig* _gtc) : gtc(_gtc) {
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
    }

    void parse_env();

    // reset the epoch system. Maybe put it in the constructor later on.
    void reset(){
        task_num = gtc->task_num;
        if (!epoch_container){
            epoch_container = new Epoch();
            epoch_container->blktype = EPOCH;
            global_epoch = &epoch_container->global_epoch;
        }
        global_epoch->store(INIT_EPOCH, std::memory_order_relaxed);
        parse_env();

        // if (uid_generator){
        //     delete uid_generator;
        // }
        // uid_generator = new UIDGenerator(gtc->task_num);
    }

    void simulate_crash(){
        if(pds::_tid==0){
            delete epoch_advancer;
            epoch_advancer = nullptr;
        }
        Persistent::simulate_crash(pds::_tid);
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

    if (c == NULL_EPOCH){
        // register alloc before BEGIN_OP, return. Will be done by
        // the BEGIN_OP that calls this again with a non-NULL c.
        return b;
    }
    PBlk* blk = b;
    blk->epoch = c;
    // Wentao: It's possible that payload is registered multiple times
    assert(blk->blktype == INIT || blk->blktype == OWNED || 
           blk->blktype == ALLOC); 
    if (blk->blktype == INIT){
        blk->blktype = ALLOC;
    }
    // to_be_persisted[c%4].push(blk);
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
    ret->epoch = c;
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
    ret->epoch = c;
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

/*
 * Below is the API for nonblocking data structures which provide:
 *  atomic_dword_t: Atomic double word for storing pointers that point
 *                  to nodes, which link payloads in.
 */
struct cas_desc_t;
struct dword_t{
    uint64_t val;
    uint64_t cnt; // least significant 1 bit in count: 0 is val, 1 is desc
    inline bool is_desc(){
        return (cnt & 1UL) == 1UL;
    }
    inline cas_desc_t* get_desc(){
        assert(is_desc());
        return reinterpret_cast<cas_desc_t*>(val);
    }
    template <typename T>
    inline T get_val(){
        static_assert(sizeof(T) == sizeof(uint64_t), "sizes do not match");
        return reinterpret_cast<T>(val);
    }
    dword_t(uint64_t v, uint64_t c) : val(v), cnt(c) {};
    dword_t() : dword_t(0, 2) {};
}__attribute__((aligned(16)));


extern thread_local uint64_t local_cnt;
extern thread_local cas_desc_t local_desc;
extern EpochSys* esys;
extern padded<uint64_t>* epochs;

template <typename T = uint64_t>
class atomic_dword_t{
    static_assert(sizeof(T) == sizeof(uint64_t), "sizes do not match");
public:
    std::atomic<dword_t> dword;
    T load();
    T load_linked();
    bool store_conditional(T expected, const T& desired);
    void store(const T& desired);
    atomic_dword_t(const T& v) : dword(dword_t(reinterpret_cast<uint64_t>(v), 2)){};
    atomic_dword_t() : atomic_dword_t(T()){};
};

struct cas_desc_t{
    enum status_t {IN_PROGRESS = 0, COMMITTED = 1, ABORTED = 2};
    std::atomic<status_t> status;
    const uint64_t cnt; // previous cnt+1
    atomic_dword_t<>* const addr;
    const uint64_t old_val;
    const uint64_t new_val;
    const uint64_t cas_epoch;
    inline void abort(){
        status_t expected = IN_PROGRESS;
        status.compare_exchange_strong(expected, ABORTED);
    }
    inline void commit(){
        status_t expected = IN_PROGRESS;
        status.compare_exchange_strong(expected, COMMITTED);
    }
    inline void complete(EpochSys* esys){
        if(esys->check_epoch(cas_epoch)){
            commit();
        } else {
            abort();
        }
    }
    inline bool committed(){
        return status.load() == COMMITTED;
    }
    inline bool in_progress(){
        return status.load() == IN_PROGRESS;
    }
    uint64_t cleanup(){
        // must be called after desc is aborted or committed
        status_t cur_status = status.load();
        assert(cur_status!=IN_PROGRESS);
        dword_t expected(reinterpret_cast<uint64_t>(addr),cnt);
        if(cur_status == COMMITTED) {
            addr->dword.compare_exchange_strong(expected, 
                                                dword_t(new_val,cnt+1));
            return new_val;
        } else {
            //aborted
            addr->dword.compare_exchange_strong(expected, 
                                                dword_t(old_val,cnt+1));
            return old_val;
        }
    }

    cas_desc_t( uint64_t c, atomic_dword_t<>* a, uint64_t o, 
                uint64_t n, uint64_t e) : 
        status(IN_PROGRESS), cnt(c), addr(a), 
        old_val(o), new_val(n), cas_epoch(e){};
    cas_desc_t() : cas_desc_t(0,nullptr,0,0,0){};
};

template<typename T>
T atomic_dword_t<T>::load(){
    dword_t r = dword.load();
    if(r.is_desc()) {
        cas_desc_t* D = r.get_desc();
        if(D->in_progress()){
            D->complete(esys);
        }
        return reinterpret_cast<T>(D->cleanup());
    } else {
        return r.get_val<T>();
    }
}

template<typename T>
T atomic_dword_t<T>::load_linked(){
    dword_t r = dword.load();
    if(r.is_desc()) {
        cas_desc_t* D = r.get_desc();
        if(D->in_progress()){
            D->complete(esys);
        }
        local_cnt = D->cnt+1;
        return reinterpret_cast<T>(D->cleanup());
    } else {
        local_cnt = r.cnt;
        return r.get_val<T>();
    }
}
template<typename T>
bool atomic_dword_t<T>::store_conditional(T expected, const T& desired){
    assert(local_cnt != 0); // ll should be called before sc
    dword_t r = dword.load();
    if(r.is_desc()){
        cas_desc_t* D = r.get_desc();
        if(D->in_progress()){
            D->complete(esys);
        }
        uint64_t new_cnt = D->cnt;
        uint64_t new_val = D->cleanup();
        if( new_cnt!=local_cnt || 
            new_val!=reinterpret_cast<uint64_t>(expected)) {
            local_cnt = 0;
            return false;
        }
    } else {
        if( r.cnt!=local_cnt || 
            r.val!=reinterpret_cast<uint64_t>(expected)) {
            local_cnt = 0;
            return false;
        }
    }
    assert(epochs[_tid].ui != NULL_EPOCH);
    new (&local_desc) cas_desc_t((r.cnt%2==1) ? (r.cnt+1) : r.cnt, 
                                 reinterpret_cast<atomic_dword_t<>*>(this), 
                                 reinterpret_cast<uint64_t>(expected), 
                                 reinterpret_cast<uint64_t>(desired), 
                                 epochs[_tid].ui);
    dword_t new_r(reinterpret_cast<uint64_t>(&local_desc),r.cnt+1);
    if(!dword.compare_exchange_strong(r,new_r)){
        local_cnt = 0;
        return false;
    }
    local_desc.complete(esys);
    local_desc.cleanup();
    local_cnt = 0;
    if(local_desc.committed()) return true;
    else return false;
}

template<typename T>
void atomic_dword_t<T>::store(const T& desired){
    // this function must be used only when there's no data race
    dword_t r = dword.load();
    dword_t new_r(reinterpret_cast<uint64_t>(desired),r.cnt+2);
    dword.store(new_r);
}

}

#endif
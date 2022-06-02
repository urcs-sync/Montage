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
#include "PersistTrackers.hpp"

class Recoverable;

namespace pds{
// extern std::atomic<size_t> abort_cnt;
// extern std::atomic<size_t> total_cnt;
struct OldSeeNewException : public std::exception {
    const char * what () const throw () {
        return "OldSeeNewException not handled.";
    }
};

enum PBlkType {INIT, ALLOC, UPDATE, DELETE, RECLAIMED, EPOCH, OWNED, DESC};

class EpochSys;

/////////////////////////////
// PBlk-related structures //
/////////////////////////////

class PBlk{
    friend class EpochSys;
    friend class nbEpochSys;
    friend class Recoverable;
protected:
    // Wentao: the first word should NOT be any persistent value for
    // epoch-system-level recovery (i.e., epoch), as Ralloc repurposes the first
    // word for block free list, which may interfere with the recovery.
    // Currently we use (transient) "reserved" as the first word. If we decide to
    // remove this field, we need to either prepend another dummy word, or
    // change the block free list in Ralloc.

    /* the first field is 64bit vptr */

    // transient.
    PBlk* retire = nullptr;

    /* 
     * Wentao: Please keep the order of the members below consistent
     * with those in sc_desc_t! There will be a reinterpret_cast
     * between these two during recovery.
     */
    uint64_t epoch = NULL_EPOCH;
    PBlkType blktype = INIT;
    // 16MSB for tid, 48LSB for sn; for nbEpochSys
    uint64_t tid_sn = 0;
    // uint64_t owner_id = 0; // TODO: make consider abandon this field and use id all the time.
    uint64_t id = 0;

public:
    void set_epoch(uint64_t e){
        // only for testing
        epoch=e;
    }
    uint64_t get_epoch(){
        return epoch;
    }
    // id gets inited by EpochSys instance.
    PBlk(): retire(nullptr), epoch(NULL_EPOCH), blktype(INIT)/*, owner_id(0)*/{}
    // id gets inited by EpochSys instance.
    PBlk(const PBlk* owner):
        retire(nullptr), blktype(OWNED)/*, owner_id(owner->blktype==OWNED? owner->owner_id : owner->id)*/ {}
    PBlk(const PBlk& oth): retire(nullptr), blktype(oth.blktype==OWNED? OWNED:INIT)/*, owner_id(oth.owner_id)*/, id(oth.id) {}
    inline uint64_t get_id() {return id;}
    virtual pptr<PBlk> get_data() {return nullptr;}
    virtual ~PBlk(){
        // Wentao: we need to zeroize epoch and flush it, avoiding it left after free
        epoch = NULL_EPOCH;
        // persist_func::clwb(&epoch);
    }

    /* functions for nbEpochSys */
    inline uint64_t get_tid() const {
        return (0xffffULL<<48 & tid_sn)>>48;
    }
    inline uint64_t get_sn() const {
        return 0xffffffffffffULL & tid_sn;
    }
    inline void set_tid_sn(uint64_t tid, uint64_t sn){
        assert(tid < 65536 && sn <= 0xffffffffffffULL);
        tid_sn = (tid<<48) | (sn & 0xffffffffffffULL);
    }
    inline void set_tid(uint64_t tid){
        assert(tid < 65536);
        tid_sn = (tid<<48) | (tid_sn & 0xffffffffffffULL);
    }
    inline void increment_sn(){
        assert((tid_sn&0xffffffffffffULL) != 0xffffffffffffULL);
        tid_sn++;
    }
    inline bool retired(){
        return retire != nullptr;
    }
};

template<typename T>
class PBlkArray : public PBlk{
    friend class EpochSys;
    friend class nbEpochSys;
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

template <class T>
class atomic_lin_var;
class lin_var{
    template <class T>
    friend class atomic_lin_var;
    inline bool is_desc() const {
        return (cnt & 3UL) == 1UL;
    }
    inline sc_desc_t* get_desc() const {
        assert(is_desc());
        return reinterpret_cast<sc_desc_t*>(val);
    }
public:
    uint64_t val;
    uint64_t cnt;
    template <typename T=uint64_t>
    inline T get_val() const {
        static_assert(sizeof(T) == sizeof(uint64_t), "sizes do not match");
        return reinterpret_cast<T>(val);
    }
    lin_var(uint64_t v, uint64_t c = 0) : val(v), cnt(c) {};
    lin_var() : lin_var(0, 0) {};

    inline bool operator==(const lin_var & b) const{
        return val==b.val && cnt==b.cnt;
    }
    inline bool operator!=(const lin_var & b) const{
        return !operator==(b);
    }
}__attribute__((aligned(16)));

template <class T = uint64_t>
class atomic_lin_var{
    static_assert(sizeof(T) == sizeof(uint64_t), "sizes do not match");
public:
    // for cnt in var:
    // desc: ....01
    // real val: ....00
    std::atomic<lin_var> var;
    T load(Recoverable* ds);
    T load_verify(Recoverable* ds);
    bool CAS_verify(Recoverable* ds, T expected, const T& desired);
    // CAS doesn't check epoch nor cnt
    bool CAS(Recoverable* ds, T expected, const T& desired);
    void store(Recoverable* ds,const T& desired);
    void store_verify(Recoverable* ds,const T& desired);

    atomic_lin_var(const T& v) : var(lin_var(reinterpret_cast<uint64_t>(v), 0)){};
    atomic_lin_var() : atomic_lin_var(T()){};
};

struct alignas(64) sc_desc_t{
protected:
    friend class EpochSys;
    friend class nbEpochSys;
    friend class Recoverable;
    // Wentao: the first word should NOT be any persistent value for
    // epoch-system-level recovery (i.e., epoch), as Ralloc repurposes the first
    // word for block free list, which may interfere with the recovery.
    // Currently we use (transient) "reserved" as the first word. If we decide to
    // remove this field, we need to either prepend another dummy word, or
    // change the block free list in Ralloc.

    // transient.
    uint64_t old_val;
    uint64_t new_val;

    /* 
     * Wentao: Please keep the order of the members below consistent
     * with those in sc_desc_t! There will be a reinterpret_cast
     * between these two during recovery.
     */
    uint64_t epoch = NULL_EPOCH;
    PBlkType blktype = DESC;
    // 16MSB for tid, 48LSB for sn; for nbEpochSys
    uint64_t tid_sn = 0;
    // for cnt in var:
    // in progress: ....01
    // committed: ....10 
    // aborted: ....11
    std::atomic<lin_var> var;

    inline bool abort(lin_var _d){
        // bring cnt from ..01 to ..11
        lin_var expected (_d.val, (_d.cnt & ~0x3UL) | 1UL); // in progress
        lin_var desired(expected);
        desired.cnt += 2;
        return var.compare_exchange_strong(expected, desired);
    }
    inline bool commit(lin_var _d){
        // bring cnt from ..01 to ..10
        lin_var expected (_d.val, (_d.cnt & ~0x3UL) | 1UL); // in progress
        lin_var desired(expected);
        desired.cnt += 1;
        return var.compare_exchange_strong(expected, desired);
    }
    inline bool committed(lin_var _d) const {
        return (_d.cnt & 0x3UL) == 2UL;
    }
    inline bool in_progress(lin_var _d) const {
        return (_d.cnt & 0x3UL) == 1UL;
    }
    inline bool match(lin_var old_d, lin_var new_d) const {
        return ((old_d.cnt & ~0x3UL) == (new_d.cnt & ~0x3UL)) && 
            (old_d.val == new_d.val);
    }
    void cleanup(lin_var old_d){
        // must be called after desc is aborted or committed
        lin_var new_d = var.load();
        if(!match(old_d,new_d)) return;
        assert(!in_progress(new_d));
        lin_var expected(reinterpret_cast<uint64_t>(this),(new_d.cnt & ~0x3UL) | 1UL);
        if(committed(new_d)) {
            // bring cnt from ..10 to ..00
            reinterpret_cast<atomic_lin_var<>*>(
                new_d.val)->var.compare_exchange_strong(
                expected, 
                lin_var(new_val,new_d.cnt + 2));
        } else {
            //aborted
            // bring cnt from ..11 to ..00
            reinterpret_cast<atomic_lin_var<>*>(
                new_d.val)->var.compare_exchange_strong(
                expected, 
                lin_var(old_val,new_d.cnt + 1));
        }
    }
public:
    void set_epoch(uint64_t e){
        // only for testing
        epoch=e;
    }
    uint64_t get_epoch(){
        return epoch;
    }
    ~sc_desc_t(){
        // Wentao: we need to zeroize epoch and flush it, avoiding it
        // left after free
        // shouldn't be called
        epoch = NULL_EPOCH;
        // persist_func::clwb(&epoch);
    }

    /* functions for nbEpochSys */
    inline uint64_t get_tid() const {
        return (0xffffULL<<48 & tid_sn)>>48;
    }
    inline uint64_t get_sn() const {
        return 0xffffffffffffULL & tid_sn;
    }
    inline void set_tid_sn(uint64_t tid, uint64_t sn){
        assert(tid < 65536 && sn <= 0xffffffffffffULL);
        tid_sn = (tid<<48) | (sn & 0xffffffffffffULL);
    }
    inline void set_tid(uint64_t tid){
        assert(tid < 65536);
        tid_sn = (tid<<48) | (tid_sn & 0xffffffffffffULL);
    }
    inline void increment_sn(){
        assert((tid_sn&0xffffffffffffULL) != 0xffffffffffffULL);
        tid_sn++;
    }

    inline bool committed() const {
        return committed(var.load());
    }
    inline bool in_progress() const {
        return in_progress(var.load());
    }
    // TODO: try_complete used to be inline. Try to make it inline again when refactoring is finished.
    void try_complete(Recoverable* ds, uint64_t addr);

    void try_abort(uint64_t expected_e);

    inline void reinit(){
        // reinit local descriptor in begin_op
        increment_sn();
        var.store(lin_var(0,0));// reset status
    }
    inline void set_up_epoch(uint64_t e){
        // set up epoch in begin_op
        epoch = e;
    }
    void set_up_var(uint64_t c, uint64_t a, uint64_t o, uint64_t n){
        // set up descriptor in CAS
        var.store(lin_var(a,c));
        old_val = o;
        new_val = n;
    }
    sc_desc_t(uint64_t t) : old_val(0), new_val(0), 
        epoch(NULL_EPOCH), blktype(DESC), tid_sn(0),
        var(lin_var(0,0)) {
            set_tid_sn(t, 0);
        };
    sc_desc_t() : sc_desc_t(0){};
};
static_assert(sizeof(sc_desc_t)==64, "the size of sc_desc_t exceeds 64!");

class EpochSys{
protected:
    // persistent fields:
    Epoch* epoch_container = nullptr;
    std::atomic<uint64_t>* global_epoch = nullptr;
    // local descriptors for DCSS
    sc_desc_t** local_descs = nullptr;

    // semi-persistent fields:
    // TODO: set a periodic-updated persistent boundary to recover to.
    UIDGenerator uid_generator;

    // transient fields:
    TransactionTracker* trans_tracker = nullptr;
    ToBePersistContainer* to_be_persisted = nullptr;
    ToBeFreedContainer* to_be_freed = nullptr;
    EpochAdvancer* epoch_advancer = nullptr;
    PersistTracker* persisted_epochs = nullptr;

    GlobalTestConfig* gtc = nullptr;
    Ralloc* _ral = nullptr;
    int task_num;
    static std::atomic<int> esys_num;
    padded<uint64_t>* last_epochs = nullptr;
    std::unordered_map<uint64_t, PBlk*>* recovered = nullptr;

public:

    /* static */
    static thread_local int tid;
    
    // system mode that toggles on/off PDELETE for recovery purpose.
    SysMode sys_mode = ONLINE;

    EpochSys(GlobalTestConfig* _gtc) : uid_generator(_gtc->task_num), gtc(_gtc) {
        std::string heap_name = get_ralloc_heap_name();
        // task_num+1 to construct Ralloc for dedicated epoch advancer
        _ral = new Ralloc(_gtc->task_num+1,heap_name.c_str(),REGION_SIZE);
        local_descs = new sc_desc_t* [gtc->task_num];
        last_epochs = new padded<uint64_t>[_gtc->task_num];
        // [wentao] FIXME: this may need to change if recovery reuses
        // existing descs
        bool restart=_ral->is_restart();
        if (restart) {
            int rec_thd = _gtc->task_num;
            if (_gtc->checkEnv("RecoverThread")){
                rec_thd = stoi(_gtc->getEnv("RecoverThread"));
            }
            auto begin = chrono::high_resolution_clock::now();
            recovered = recover(rec_thd);
            auto end = chrono::high_resolution_clock::now();
            auto dur = end - begin;
            auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            std::cout << "Spent " << dur_ms << "ms getting PBlk(" << recovered->size() << ")" << std::endl;
        }
        for(int i=0;i<gtc->task_num;i++){
            local_descs[i] = new_pblk<sc_desc_t>(i);
            last_epochs[i].ui = NULL_EPOCH;
            assert(local_descs[i]!=nullptr);
            persist_func::clwb_range_nofence(local_descs[i],sizeof(sc_desc_t));
        }
        persist_func::sfence();
        if (!restart) {
            reset();
        }
        
    }

    // void flush(){
    //     for (int i = 0; i < 2; i++){
    //         sync(NULL_EPOCH);
    //     }
    // }

    virtual ~EpochSys(){
        // std::cout<<"epochsys descructor called"<<std::endl;
        trans_tracker->finalize();
        // flush(); // flush is done in epoch_advancer's destructor.
        if (epoch_advancer){
            delete epoch_advancer;
        }
        if(local_descs){
            delete local_descs;
        }
        if (gtc->verbose){
            std::cout<<"final epoch:"<<global_epoch->load()<<std::endl;
        }
        
        delete trans_tracker;
        delete persisted_epochs;
        delete to_be_persisted;
        delete to_be_freed;
        // Wentao: Due to the lack of snapshotting on transient
        // indexing, we are unable to do fast recovery from clean
        // exit for now. 
        // Remove the following `set_fake_dirty()` routine if we
        // eventually support snapshot and fast recovery from clean
        // exit. 
        _ral->set_fake_dirty();
        delete _ral;
        delete last_epochs;
        // std::cout<<"Aborted:Total = "<<abort_cnt.load()<<":"<<total_cnt.load()<<std::endl;
    }

    virtual void parse_env();

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

    virtual void reset(){
        task_num = gtc->task_num;
        if (!epoch_container){
            epoch_container = new_pblk<Epoch>();
            epoch_container->blktype = EPOCH;
            global_epoch = &epoch_container->global_epoch;
        }
        global_epoch->store(INIT_EPOCH, std::memory_order_relaxed);
        parse_env();
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
    void delete_pblk(T* pblk, uint64_t c){
        pblk->~T();
        _ral->deallocate(pblk);
        if (sys_mode == ONLINE && c != NULL_EPOCH){
            if (EpochSys::tid >= gtc->task_num){
                // if this thread does not have to-be-presisted buffer
                persist_func::clwb(pblk);
            } else {
                to_be_persisted->register_persist_raw((PBlk*)pblk, c);
            }
        }
    }

    // check if global is the same as c.
    bool check_epoch(uint64_t c);

    // return thread-local dcss desc
    inline sc_desc_t* get_dcss_desc(){
        return local_descs[pds::EpochSys::tid];
    }

    // start transaction in the current epoch c.
    // prevent current epoch advance from c+1 to c+2.
    // Wentao: for nonblocking persistence, epoch may advance and
    // ongoing progress will be aborted
    virtual uint64_t begin_transaction();
 
    // end transaction, release the holding of epoch increments.
    virtual void end_transaction(uint64_t c);

    // auto begin and end a reclaim-only transaction
    virtual uint64_t begin_reclaim_transaction();
    virtual void end_reclaim_transaction(uint64_t c);

    // end read only transaction, release the holding of epoch increments.
    virtual void end_readonly_transaction(uint64_t c);

    // abort transaction, release the holding of epoch increments without other traces.
    virtual void abort_transaction(uint64_t c);

    // validate an access in epoch c. throw exception if last update is newer than c.
    void validate_access(const PBlk* b, uint64_t c);

    // register the allocation of a PBlk during a transaction.
    // called for new blocks at both pnew (holding them in
    // pending_allocs) and begin_op (registering them with the
    // acquired epoch).
    virtual void register_alloc_pblk(PBlk* b, uint64_t c);

    template<typename T>
    T* reset_alloc_pblk(T* b, uint64_t c);

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

    // for blocking persistence, buffer retire requests.
    virtual void prepare_retire_pblk(PBlk* b, const uint64_t& c, std::vector<std::pair<PBlk*,PBlk*>>& pending_retires);
    virtual void prepare_retire_pblk(std::pair<PBlk*,PBlk*>& pending_retire, const uint64_t& c);

    virtual void withdraw_retire_pblk(PBlk* b, uint64_t c);

    // for blocking persistence, retire a PBlk during a transaction.
    virtual void retire_pblk(PBlk* b, uint64_t c, PBlk* anti=nullptr);

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
    void sync(){
        epoch_advancer->sync(last_epochs[tid].ui);
    }

    /////////////////
    // Bookkeeping //
    /////////////////

    // get the current global epoch number.
    uint64_t get_epoch();

    bool epoch_CAS(uint64_t& expected, const uint64_t& desired){
        return global_epoch->compare_exchange_strong(expected, desired);
    }

    // // try to advance global epoch, helping others along the way.
    // void advance_epoch(uint64_t c);

    // // a version of advance_epoch for a SINGLE bookkeeping thread.
    // void advance_epoch_dedicated();

    // The following bookkeeping methods are for a SINGLE bookkeeping thread:

    // atomically set the current global epoch number
    void set_epoch(uint64_t c);

    // stuff to do at the beginning of epoch c
    virtual void on_epoch_begin(uint64_t c);

    // stuff to do at the end of epoch c
    virtual void on_epoch_end(uint64_t c);

    /////////////
    // Recover //
    /////////////
    
    std::unordered_map<uint64_t, PBlk*>* get_recovered() {
        return (recovered);
    }

    // recover all PBlk decendants. return an iterator.
    virtual std::unordered_map<uint64_t, PBlk*>* recover(const int rec_thd = 2);
};

class nbEpochSys : public EpochSys {
    // clean up thread-local to_be_freed buckets in begin_op starts in epoch c
    void local_free(uint64_t c);
    // clean up thread-local to_be_persisted buckets in begin_op
    // starts in epoch c; this must contain only reset payloads
    void local_persist(uint64_t c);
   public:
    virtual void reset() override;
    virtual void parse_env() override;
    virtual uint64_t begin_transaction() override;
    virtual void end_transaction(uint64_t c) override;
    virtual uint64_t begin_reclaim_transaction() override;
    virtual void end_reclaim_transaction(uint64_t c) override;
    virtual void end_readonly_transaction(uint64_t c) override{
        last_epochs[tid] = c;
    };
    virtual void abort_transaction(uint64_t c) override{
        last_epochs[tid] = c;
    };
    virtual void on_epoch_begin(uint64_t c) override;
    virtual void on_epoch_end(uint64_t c) override;
    virtual std::unordered_map<uint64_t, PBlk*>* recover(const int rec_thd = 2) override;
    /*{assert(0&&"not implemented yet"); return {};}*/

    virtual void register_alloc_pblk(PBlk* b, uint64_t c) override;
   // for nonblocking persistence, prepare to retire a PBlk during a transaction.
    virtual void prepare_retire_pblk(PBlk* b, const uint64_t& c, std::vector<std::pair<PBlk*,PBlk*>>& pending_retires) override;
    virtual void prepare_retire_pblk(std::pair<PBlk*,PBlk*>& pending_retire, const uint64_t& c) override;
    virtual void withdraw_retire_pblk(PBlk* b, uint64_t c) override;

    // for nonblocking persistence, retire a PBlk during a transaction.
    virtual void retire_pblk(PBlk* b, uint64_t c, PBlk* anti=nullptr) override;

    nbEpochSys(GlobalTestConfig* _gtc) : EpochSys(_gtc){
#ifdef VISIBLE_READ
        // ensure nbEpochSys is used only when VISIBLE_READ is not
        assert(0&&"nbEpochSys is incompatible with VISIBLE_READ!");
#endif
    };
};

template<typename T>
T* EpochSys::reset_alloc_pblk(T* b, uint64_t c){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);
    PBlk* blk = b;
    blk->epoch = NULL_EPOCH;
    assert(blk->blktype == ALLOC); 
    blk->blktype = INIT;
    // bufferedly persist the first cache line of b
    to_be_persisted->register_persist_raw(blk, c);
    PBlk* data = blk->get_data();
    if (data){
        reset_alloc_pblk(data,c);
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
    // to_be_persisted->register_persist(ret, c);
    return ret;
}

template<typename T>
PBlkArray<T>* EpochSys::copy_pblk_array(const PBlkArray<T>* oth, uint64_t c){
    PBlkArray<T>* ret = static_cast<PBlkArray<T>*>(
        _ral->allocate(sizeof(PBlkArray<T>) + oth->size*sizeof(T)));
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
    // we can safely deallocate b directly if it hasn't been tagged
    // with any epoch, and return
    if(e==NULL_EPOCH){
        delete_pblk(b, c);
        return;
    }
    if (e > c){
        throw OldSeeNewException();
    } else if (e == c){
        if (blktype == ALLOC){
            delete_pblk(b, c);
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
        to_be_persisted->register_persist(del, c);
        // to_be_freed[(c+1)%4].push(del);
        to_be_freed->register_free(del, c+1);
    }
    // to_be_freed[c%4].push(b);
    to_be_freed->register_free(b, c);
}

template<typename T>
void EpochSys::reclaim_pblk(T* b, uint64_t c){
    ASSERT_DERIVE(T, PBlk);
    ASSERT_COPY(T);
    if(c==NULL_EPOCH){
        errexit("reclaiming a block in NULL epoch");
    }
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
            delete_pblk(b, c);
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
            delete_pblk(b, c);
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
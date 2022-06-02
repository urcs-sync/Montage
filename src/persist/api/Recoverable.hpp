#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include <immintrin.h>
// TODO: report recover errors/exceptions

class Recoverable;

namespace pds{
    ////////////////////////////////////////
    // counted pointer-related structures //
    ////////////////////////////////////////

    /*
    * Macro VISIBLE_READ determines which version of API will be used.
    * Macro USE_TSX determines whether TSX (Intel HTM) will be used.
    * 
    * We highly recommend you to use default invisible read version,
    * since it doesn't need you to handle EpochVerifyException and you
    * can call just load rather than load_verify throughout your program
    * 
    * We provides following double-compare-single-swap (DCSS) API for
    * nonblocking data structures to use: 
    * 
    *  atomic_lin_var<T=uint64_t>: atomic double word for storing pointers
    *  that point to nodes, which link payloads in. It contains following
    *  functions:
    * 
    *      store(T val): 
    *          store 64-bit long data without sync; cnt doesn't increment
    * 
    *      store(lin_var d): store(d.val)
    * 
    *      lin_var load(): 
    *          load var without verifying epoch
    * 
    *      lin_var load_verify(): 
    *          load var and verify epoch, used as lin point; 
    *          for invisible reads this won't verify epoch
    * 
    *      bool CAS(lin_var expected, T desired): 
    *          CAS in desired value and increment cnt if expected 
    *          matches current var
    * 
    *      bool CAS_verify(lin_var expected, T desired): 
    *          CAS in desired value and increment cnt if expected 
    *          matches current var and global epoch doesn't change
    *          since BEGIN_OP
    */

    struct EpochVerifyException : public std::exception {
        const char * what () const throw () {
            return "Epoch in which operation wants to linearize has passed; retry required.";
        }
    };
}

class Recoverable{
    pds::EpochSys* _esys = nullptr;
    
    // current epoch of each thread.
    padded<uint64_t>* epochs = nullptr;
    // containers for pending allocations
    padded<std::vector<pds::PBlk*>>* pending_allocs = nullptr;
    // pending retires; each pair is <original payload, anti-payload>
    padded<std::vector<pair<pds::PBlk*,pds::PBlk*>>>* pending_retires = nullptr;
    // pointer to recovered PBlks from EpochSys
    std::unordered_map<uint64_t, pds::PBlk *>* recovered_pblks = nullptr;
    // count of last recovered PBlks from EpochSys
    uint64_t last_recovered_cnt = 0;
public:
    // return num of blocks recovered.
    virtual int recover() {
        errexit("recover() not implemented. Implement recover() or delete existing persistent heap file.");
        return 0;
    }
    Recoverable(GlobalTestConfig* gtc);
    virtual ~Recoverable();

    void init_thread(GlobalTestConfig*, LocalTestConfig* ltc);
    void init_thread(int tid);
    bool check_epoch(){
        return _esys->check_epoch(epochs[pds::EpochSys::tid].ui);
    }
    bool check_epoch(uint64_t c){
        return _esys->check_epoch(c);
    }
    void begin_op(){
        assert(epochs[pds::EpochSys::tid].ui == NULL_EPOCH);
        epochs[pds::EpochSys::tid].ui = _esys->begin_transaction();
        for(auto & r : pending_retires[pds::EpochSys::tid].ui) {
            // for nonblocking, create anti-nodes for retires called
            // before begin_op, place anti-nodes into pending_retires,
            // and set tid_sn
            // for blocking, just noop
            _esys->prepare_retire_pblk(r,epochs[pds::EpochSys::tid].ui);
        }
        // TODO: any room for optimization here?
        // TODO: put pending_allocs-related stuff into operations?
        for (auto b = pending_allocs[pds::EpochSys::tid].ui.begin(); 
            b != pending_allocs[pds::EpochSys::tid].ui.end(); b++){
            assert((*b)->get_epoch() == NULL_EPOCH);
            _esys->register_alloc_pblk(*b, epochs[pds::EpochSys::tid].ui);
        }
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
    }
    void end_op(){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        if (!pending_retires[pds::EpochSys::tid].ui.empty()){
            for(const auto& r : pending_retires[pds::EpochSys::tid].ui){
                // for nbEpochSys, link anti-node to payload
                // for EpochSys,in-place retire or create anti-node,
                // and register in to_be_persisted
                _esys->retire_pblk(r.first, epochs[pds::EpochSys::tid].ui, r.second);
            }
            pending_retires[pds::EpochSys::tid].ui.clear();
        }
        if (epochs[pds::EpochSys::tid].ui != NULL_EPOCH){
            _esys->end_transaction(epochs[pds::EpochSys::tid].ui);
            epochs[pds::EpochSys::tid].ui = NULL_EPOCH;
        }
        if(!pending_allocs[pds::EpochSys::tid].ui.empty()) 
            pending_allocs[pds::EpochSys::tid].ui.clear();
    }
    void end_readonly_op(){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        if (epochs[pds::EpochSys::tid].ui != NULL_EPOCH){
            _esys->end_readonly_transaction(epochs[pds::EpochSys::tid].ui);
            epochs[pds::EpochSys::tid].ui = NULL_EPOCH;
        }
        assert(pending_allocs[pds::EpochSys::tid].ui.empty());
        assert(pending_retires[pds::EpochSys::tid].ui.empty());
    }
    void abort_op(){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        if(!pending_retires[pds::EpochSys::tid].ui.empty()){
            for(const auto& r : pending_retires[pds::EpochSys::tid].ui){
                _esys->withdraw_retire_pblk(r.second,epochs[pds::EpochSys::tid].ui);
            }
            pending_retires[pds::EpochSys::tid].ui.clear();
        }
        // TODO: any room for optimization here?
        for (auto b = pending_allocs[pds::EpochSys::tid].ui.begin(); 
            b != pending_allocs[pds::EpochSys::tid].ui.end(); b++){
            // reset epochs registered in pending blocks
            _esys->reset_alloc_pblk(*b,epochs[pds::EpochSys::tid].ui);
        }
        _esys->abort_transaction(epochs[pds::EpochSys::tid].ui);
        epochs[pds::EpochSys::tid].ui = NULL_EPOCH;
    }
    class MontageOpHolder{
        Recoverable* ds = nullptr;
    public:
        MontageOpHolder(Recoverable* ds_): ds(ds_){
            ds->begin_op();
        }
        ~MontageOpHolder(){
            ds->end_op();
        }
    };
    class MontageOpHolderReadOnly{
        Recoverable* ds = nullptr;
    public:
        MontageOpHolderReadOnly(Recoverable* ds_): ds(ds_){
            ds->begin_op();
        }
        ~MontageOpHolderReadOnly(){
            ds->end_readonly_op();
        }
    };
    pds::PBlk* pmalloc(size_t sz) 
    {
        pds::PBlk* ret = (pds::PBlk*)_esys->malloc_pblk(sz);
        if (epochs[pds::EpochSys::tid].ui == NULL_EPOCH){
            pending_allocs[pds::EpochSys::tid].ui.push_back(ret);
        } else {
            _esys->register_alloc_pblk(ret, epochs[pds::EpochSys::tid].ui);
        }
        return (pds::PBlk*)ret;
    }
    template <typename T, typename... Types> 
    T* pnew(Types... args) 
    {
        T* ret = _esys->new_pblk<T>(args...);
        if (epochs[pds::EpochSys::tid].ui == NULL_EPOCH){
            pending_allocs[pds::EpochSys::tid].ui.push_back(ret);
        } else {
            _esys->register_alloc_pblk(ret, epochs[pds::EpochSys::tid].ui);
        }
        return ret;
    }

    template<typename T>
    void register_update_pblk(T* b){
        _esys->register_update_pblk(b, epochs[pds::EpochSys::tid].ui);
    }
    template<typename T>
    void pdelete(T* b){
        ASSERT_DERIVE(T, pds::PBlk);
        ASSERT_COPY(T);

        if (_esys->sys_mode == pds::ONLINE){
            if (epochs[pds::EpochSys::tid].ui != NULL_EPOCH){
                _esys->free_pblk(b, epochs[pds::EpochSys::tid].ui);
            } else {
                if (((pds::PBlk*)b)->get_epoch() == NULL_EPOCH){
                    std::reverse_iterator pos = std::find(pending_allocs[pds::EpochSys::tid].ui.rbegin(),
                        pending_allocs[pds::EpochSys::tid].ui.rend(), b);
                    assert(pos != pending_allocs[pds::EpochSys::tid].ui.rend());
                    pending_allocs[pds::EpochSys::tid].ui.erase((pos+1).base());
                }
                _esys->delete_pblk(b, epochs[pds::EpochSys::tid].ui);
            }
        }
    }
    /* 
     * pretire() must be called BEFORE lin point, i.e., CAS_verify()!
     *
     * For nonblocking persistence, retirement will be initiated (by
     * craeating anti-node) at (if called before) begin_op, and will
     * be committed in end_op or withdrew (by deleting anti-node) at
     * abort_op;
     *
     * For blocking persistence, retirement will be buffered until
     * end_op, at which it will be initiated and commited (by creating
     * anti-node), or will withdrew (by deleting anti-node) at
     * abort_op.
     */
    template<typename T>
    void pretire(T* b){
        if(epochs[pds::EpochSys::tid].ui == NULL_EPOCH){
            // buffer retirement in pending_retires; it will be
            // initiated at begin_op
            pending_retires[pds::EpochSys::tid].ui.emplace_back(b, nullptr);
        } else {
            // for nonblocking, place anti-nodes and retires into
            // pending_retires and set tid_sn
            // for blocking, buffer retire request in pending_retires
            // to be committed at end_op or withdrew at abort_op
            _esys->prepare_retire_pblk(b, epochs[pds::EpochSys::tid].ui, pending_retires[pds::EpochSys::tid].ui);
        }
    }
    template<typename T>
    void preclaim(T* b){
        if (_esys->sys_mode == pds::ONLINE){
            bool not_in_operation = false;
            if (epochs[pds::EpochSys::tid].ui == NULL_EPOCH){
                not_in_operation = true;
                epochs[pds::EpochSys::tid].ui = _esys->begin_reclaim_transaction();
                if(!b->retired()){
                    // WARNING!!!
                    // Wentao: here we optimize so that only nodes not
                    // ever published (and thus not ever retired) will
                    // get into this part. This is only applicable for
                    // nonblocking data structures. For blocking ones,
                    // don't call nb-specific pretire and preclaim,
                    // but instead call pdelete!
                    std::reverse_iterator pos = std::find(pending_allocs[pds::EpochSys::tid].ui.rbegin(),
                            pending_allocs[pds::EpochSys::tid].ui.rend(), b);
                    if(pos != pending_allocs[pds::EpochSys::tid].ui.rend()){
                        pending_allocs[pds::EpochSys::tid].ui.erase((pos+1).base());
                    }
                }
            }
            _esys->reclaim_pblk(b, epochs[pds::EpochSys::tid].ui);
            if (not_in_operation){
                _esys->end_reclaim_transaction(epochs[pds::EpochSys::tid].ui);
                epochs[pds::EpochSys::tid].ui = NULL_EPOCH;
            }
        }
    }
    template<typename T>
    const T* openread_pblk(const T* b){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        return _esys->openread_pblk(b, epochs[pds::EpochSys::tid].ui);
    }
    template<typename T>
    const T* openread_pblk_unsafe(const T* b){
        // Wentao: skip checking epoch here since this may be called
        // during recovery, which may not have epochs[tid]
        // if (epochs[pds::EpochSys::tid].ui != NULL_EPOCH){
        //     return _esys->openread_pblk_unsafe(b, epochs[pds::EpochSys::tid].ui);
        // } else {
            return b;
        // }
    }
    template<typename T>
    T* openwrite_pblk(T* b){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        return _esys->openwrite_pblk(b, epochs[pds::EpochSys::tid].ui);
    }
    std::unordered_map<uint64_t, pds::PBlk*>* get_recovered_pblks(){
        return recovered_pblks;
    }
    uint64_t get_last_recovered_cnt() {
        return last_recovered_cnt;
    }
    void sync(){
        assert(epochs[pds::EpochSys::tid].ui == NULL_EPOCH);
        _esys->sync();
    }
    void recover_mode(){
        _esys->sys_mode = pds::RECOVER; // PDELETE -> nop
    }
    void online_mode(){
        _esys->sys_mode = pds::ONLINE;
    }
    void flush(){
        for (int i = 0; i < 2; i++){
            sync();
        }
        // _esys->flush();
    }

    pds::sc_desc_t* get_dcss_desc(){
        return _esys->get_dcss_desc();
    }
    uint64_t get_local_epoch(){
        return epochs[pds::EpochSys::tid].ui;
    }
};

/////////////////////////////
// field generation macros //
/////////////////////////////

// macro for concatenating two tokens into a new token
#define TOKEN_CONCAT(a,b)  a ## b

/**
 *  using the type t and the name n, generate a protected declaration for the
 *  field, as well as public getters and setters
 */
#define GENERATE_FIELD(t, n, T)\
/* declare the field, with its name prefixed by m_ */\
protected:\
    t TOKEN_CONCAT(m_, n);\
public:\
/* get method open a pblk for read. */\
t TOKEN_CONCAT(get_, n)(Recoverable* ds) const{\
    return ds->openread_pblk(this)->TOKEN_CONCAT(m_, n);\
}\
/* get method open a pblk for read. Allows old-see-new reads. */\
t TOKEN_CONCAT(get_unsafe_, n)(Recoverable* ds) const{\
    return ds->openread_pblk_unsafe(this)->TOKEN_CONCAT(m_, n);\
}\
/* set method open a pblk for write. return a new copy when necessary */\
template <class in_type>\
T* TOKEN_CONCAT(set_, n)(Recoverable* ds, const in_type& TOKEN_CONCAT(tmp_, n)){\
    assert(ds->get_local_epoch() != NULL_EPOCH);\
    auto ret = ds->openwrite_pblk(this);\
    ret->TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
    ds->register_update_pblk(ret);\
    return ret;\
}\
/* set the field by the parameter. called only outside BEGIN_OP and END_OP */\
template <class in_type>\
void TOKEN_CONCAT(set_unsafe_, n)(Recoverable* ds, const in_type& TOKEN_CONCAT(tmp_, n)){\
    TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
}

/**
 *  using the type t, the name n and length s, generate a protected
 *  declaration for the field, as well as public getters and setters
 */
#define GENERATE_ARRAY(t, n, s, T)\
/* declare the field, with its name prefixed by m_ */\
protected:\
    t TOKEN_CONCAT(m_, n)[s];\
/* get method open a pblk for read. */\
t TOKEN_CONCAT(get_, n)(Recoverable* ds, int i) const{\
    return ds->openread_pblk(this)->TOKEN_CONCAT(m_, n)[i];\
}\
/* get method open a pblk for read. Allows old-see-new reads. */\
t TOKEN_CONCAT(get_unsafe_, n)(Recoverable* ds, int i) const{\
    return ds->openread_pblk_unsafe(this)->TOKEN_CONCAT(m_, n)[i];\
}\
/* set method open a pblk for write. return a new copy when necessary */\
T* TOKEN_CONCAT(set_, n)(Recoverable* ds, int i, t TOKEN_CONCAT(tmp_, n)){\
    assert(ds->get_local_epoch() != NULL_EPOCH);\
    auto ret = ds->openwrite_pblk(this);\
    ret->TOKEN_CONCAT(m_, n)[i] = TOKEN_CONCAT(tmp_, n);\
    ds->register_update_pblk(ret);\
    return ret;\
}

namespace pds{

#ifdef VISIBLE_READ
    // implementation of load, store, and cas for visible reads

    template<typename T>
    void atomic_lin_var<T>::store(Recoverable* ds,const T& desired){
        lin_var r;
        while(true){
            r = var.load();
            lin_var new_r(reinterpret_cast<uint64_t>(desired),r.cnt+1);
            if(var.compare_exchange_strong(r, new_r))
                break;
        }
    }

    template<typename T>
    void atomic_lin_var<T>::store_verify(Recoverable* ds,const T& desired){
        lin_var r;
        while(true){
            r = var.load();
            if(ds->check_epoch()){
                lin_var new_r(reinterpret_cast<uint64_t>(desired),r.cnt+1);
                if(var.compare_exchange_strong(r, new_r)){
                    break;
                }
            } else {
                throw EpochVerifyException();
            }
        }
    }

    template<typename T>
    lin_var atomic_lin_var<T>::load(Recoverable* ds){
        lin_var r;
        while(true){
            r = var.load();
            lin_var ret(r.val,r.cnt+1);
            if(var.compare_exchange_strong(r, ret))
                return ret;
        }
    }

    template<typename T>
    lin_var atomic_lin_var<T>::load_verify(Recoverable* ds){
        assert(ds->get_local_epoch() != NULL_EPOCH);
        lin_var r;
        while(true){
            r = var.load();
            if(ds->check_epoch()){
                lin_var ret(r.val,r.cnt+1);
                if(var.compare_exchange_strong(r, ret)){
                    return r;
                }
            } else {
                throw EpochVerifyException();
            }
        }
    }

    template<typename T>
    bool atomic_lin_var<T>::CAS_verify(Recoverable* ds, lin_var expected, const T& desired){
        bool not_in_operation = false;
        if(ds->get_local_epoch() == NULL_EPOCH){
            ds->begin_op();
            not_in_operation = true;
        }
        assert(ds->get_local_epoch() != NULL_EPOCH);
        if(ds->check_epoch()){
            lin_var new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
            bool ret = var.compare_exchange_strong(expected, new_r);
            if(ret == true){
                if(not_in_operation) ds->end_op();
            } else {
                if(not_in_operation) ds->abort_op();
            }
            return ret;
        } else {
            if(not_in_operation) ds->abort_op();
            return false;
        }
    }

    template<typename T>
    bool atomic_lin_var<T>::CAS(lin_var expected, const T& desired){
        lin_var new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
        return var.compare_exchange_strong(expected, new_r);
    }

#else /* !VISIBLE_READ */
    /* implementation of load and cas for invisible reads */

    template<typename T>
    void atomic_lin_var<T>::store(Recoverable* ds,const T& desired){
        lin_var r;
        while(true){
            r = var.load();
            if(r.is_desc()) {
                sc_desc_t* D = r.get_desc();
                D->try_complete(ds, reinterpret_cast<uint64_t>(this));
                r.cnt &= (~0x3ULL);
                r.cnt+=4;
            }
            lin_var new_r(reinterpret_cast<uint64_t>(desired),r.cnt+4);
            if(var.compare_exchange_strong(r, new_r))
                break;
        }
    }

    template<typename T>
    void atomic_lin_var<T>::store_verify(Recoverable* ds,const T& desired){
        lin_var r;
        while(true){
            r = var.load();
            if(r.is_desc()){
                sc_desc_t* D = r.get_desc();
                D->try_complete(ds, reinterpret_cast<uint64_t>(this));
                r.cnt &= (~0x3ULL);
                r.cnt+=4;
            }
            if(ds->check_epoch()){
                lin_var new_r(reinterpret_cast<uint64_t>(desired),r.cnt+4);
                if(var.compare_exchange_strong(r, new_r)){
                    break;
                }
            } else {
                throw EpochVerifyException();
            }
        }
    }

    template<typename T>
    T atomic_lin_var<T>::load(Recoverable* ds){
        lin_var r;
        do { 
            r = var.load();
            if(r.is_desc()) {
                sc_desc_t* D = r.get_desc();
                D->try_complete(ds, reinterpret_cast<uint64_t>(this));
            }
        } while(r.is_desc());
        return (T)r.val;
    }

    template<typename T>
    T atomic_lin_var<T>::load_verify(Recoverable* ds){
        // invisible read doesn't need to verify epoch even if it's a
        // linearization point
        // this saves users from catching EpochVerifyException
        return load(ds);
    }

    template<typename T>
    bool atomic_lin_var<T>::CAS_verify(Recoverable* ds, T expected, const T& desired){
        bool not_in_operation = false;
        if(ds->get_local_epoch() == NULL_EPOCH){
            ds->begin_op();
            not_in_operation = true;
        }
        assert(ds->get_local_epoch() != NULL_EPOCH);
#ifdef USE_TSX
        // total_cnt.fetch_add(1);
        unsigned status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            lin_var r = var.load();
            if(!r.is_desc()){
                if( r.val!=reinterpret_cast<uint64_t>(expected) ||
                    !ds->check_epoch()){
                    _xend();
                    if(not_in_operation) ds->abort_op();
                    return false;
                } else {
                    lin_var new_r (reinterpret_cast<uint64_t>(desired), r.cnt+4);
                    var.store(new_r);
                    _xend();
                    if(not_in_operation) ds->end_op();
                    return true;
                }
            } else {
                // we only help complete descriptor, but not retry
                _xend();
                r.get_desc()->try_complete(ds, reinterpret_cast<uint64_t>(this));
                if(not_in_operation) ds->abort_op();
                return false;
            }
            // execution won't reach here; program should have returned
            assert(0);
        }
        // abort_cnt.fetch_add(1);
#endif
        // txn fails; fall back routine
        lin_var r = var.load();
        if(r.is_desc()){
            sc_desc_t* D = r.get_desc();
            D->try_complete(ds, reinterpret_cast<uint64_t>(this));
            if(not_in_operation) ds->abort_op();
            return false;
        } else {
            if( r.val!=reinterpret_cast<uint64_t>(expected)) {
                if(not_in_operation) ds->abort_op();
                return false;
            }
        }
        // now r.cnt must be ..00, and r.cnt+1 is ..01, which means "var
        // contains a descriptor" and "a descriptor is in progress"
        assert((r.cnt & 3UL) == 0UL);
        ds->get_dcss_desc()->set_up_var(r.cnt+1, 
                                    reinterpret_cast<uint64_t>(this), 
                                    reinterpret_cast<uint64_t>(expected), 
                                    reinterpret_cast<uint64_t>(desired));
        lin_var new_r(reinterpret_cast<uint64_t>(ds->get_dcss_desc()), r.cnt+1);
        if(!var.compare_exchange_strong(r,new_r)){
            if(not_in_operation) ds->abort_op();
            return false;
        }
        ds->get_dcss_desc()->try_complete(ds, reinterpret_cast<uint64_t>(this));
        if(ds->get_dcss_desc()->committed()) {
            if(not_in_operation) ds->end_op();
            return true;
        }
        else {
            if(not_in_operation) ds->abort_op();
            return false;
        }
    }

    template<typename T>
    bool atomic_lin_var<T>::CAS(Recoverable* ds, T expected, const T& desired){
        // CAS doesn't check epoch; just cas ptr to desired, with cnt+=4
        // assert(!expected.is_desc());
        lin_var r = var.load();
        if(r.is_desc()){
            sc_desc_t* D = r.get_desc();
            D->try_complete(ds, reinterpret_cast<uint64_t>(this));
            return false;
        }
        lin_var old_r(reinterpret_cast<uint64_t>(expected), r.cnt);
        lin_var new_r(reinterpret_cast<uint64_t>(desired), r.cnt + 4);
        if(!var.compare_exchange_strong(old_r,new_r)){
            return false;
        }
        return true;
    }

#endif /* !VISIBLE_READ */
} // namespace pds

#endif
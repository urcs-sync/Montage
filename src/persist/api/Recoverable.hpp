#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
// TODO: report recover errors/exceptions

class Recoverable{
    // TODO: get rid of these.
    template<typename T> friend class pds::atomic_lin_var;
    friend class pds::lin_var;

    pds::EpochSys* _esys = nullptr;

    // local descriptors for DCSS
    // TODO: maybe put this into a derived class for NB data structures?
    padded<pds::sc_desc_t>* local_descs = nullptr;
public:
    // return num of blocks recovered.
    virtual int recover(bool simulated = false) = 0;
    Recoverable(GlobalTestConfig* gtc);
    ~Recoverable();

    void init_thread(GlobalTestConfig*, LocalTestConfig* ltc);
    void init_thread(int tid);
    bool check_epoch(){
        return _esys->check_epoch();
    }
    bool check_epoch(uint64_t c){
        return _esys->check_epoch(c);
    }
    void begin_op(){
        _esys->begin_op();
    }
    void end_op(){
        _esys->end_op();
    }
    void end_readonly_op(){
        _esys->end_readonly_op();
    }
    void abort_op(){
        _esys->abort_op();
    }
    class MontageOpHolder{
        pds::EpochSys* esys_;
    public:
        MontageOpHolder(Recoverable* ds): esys_(ds->_esys){
            esys_->begin_op();
        }
        MontageOpHolder(pds::EpochSys* _esys): esys_(_esys){
            esys_->begin_op();
        }
        ~MontageOpHolder(){
            esys_->end_op();
        }
    };
    class MontageOpHolderReadOnly{
        pds::EpochSys* esys_;
    public:
        MontageOpHolderReadOnly(Recoverable* ds): esys_(ds->_esys){
            esys_->begin_op();
        }
        MontageOpHolderReadOnly(pds::EpochSys* _esys): esys_(_esys){
            esys_->begin_op();
        }
        ~MontageOpHolderReadOnly(){
            esys_->end_readonly_op();
        }
    };

    // pnew is in a separate file since there are a bunch of them.
    // add more as needed.
    #include "pnew.hpp"

    template<typename T>
    void register_update_pblk(T* b){
        _esys->register_update_pblk(b);
    }
    template<typename T>
    void pdelete(T* b){
        _esys->pdelete(b);
    }
    template<typename T>
    void pretire(T* b){
        _esys->pretire(b);
    }
    template<typename T>
    void preclaim(T* b){
        _esys->pdelete(b);
    }
    template<typename T>
    const T* openread_pblk(const T* b){
        return _esys->openread_pblk(b);
    }
    template<typename T>
    const T* openread_pblk_unsafe(const T* b){
        return _esys->openread_pblk_unsafe(b);
    }
    template<typename T>
    T* openwrite_pblk(T* b){
        return _esys->openwrite_pblk(b);
    }
    std::unordered_map<uint64_t, pds::PBlk*>* recover_pblks(const int rec_thd=10){
        return _esys->recover(rec_thd);
    }
    void recover_mode(){
        _esys->recover_mode();
    }
    void online_mode(){
        _esys->online_mode();
    }
    void flush(){
        _esys->flush();
    }
    void simulate_crash(){
        _esys->simulate_crash();
    }

    pds::sc_desc_t* get_dcss_desc(){
        return &local_descs[pds::EpochSys::tid].ui;
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
    assert(ds->epochs[EpochSys::tid].ui != NULL_EPOCH);\
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
    assert(ds->epochs[EpochSys::tid].ui != NULL_EPOCH);\
    auto ret = ds->openwrite_pblk(this);\
    ret->TOKEN_CONCAT(m_, n)[i] = TOKEN_CONCAT(tmp_, n);\
    ds->register_update_pblk(ret);\
    return ret;\
}

namespace pds{

    template<typename T>
    void atomic_lin_var<T>::store(const T& desired){
        // this function must be used only when there's no data race
        lin_var r = var.load();
        lin_var new_r(reinterpret_cast<uint64_t>(desired),r.cnt);
        var.store(new_r);
    }

#ifdef VISIBLE_READ
    // implementation of load and cas for visible reads

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
        assert(ds->_esys->epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        lin_var r;
        while(true){
            r = var.load();
            if(ds->_esys->check_epoch(ds->_esys->epochs[pds::EpochSys::tid].ui)){
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
        assert(ds->_esys->epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        if(ds->_esys->check_epoch(ds->_esys->epochs[pds::EpochSys::tid].ui)){
            lin_var new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
            return var.compare_exchange_strong(expected, new_r);
        } else {
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
    lin_var atomic_lin_var<T>::load(Recoverable* ds){
        lin_var r;
        do { 
            r = var.load();
            if(r.is_desc()) {
                sc_desc_t* D = r.get_desc();
                D->try_complete(ds, reinterpret_cast<uint64_t>(this));
            }
        } while(r.is_desc());
        return r;
    }

    template<typename T>
    lin_var atomic_lin_var<T>::load_verify(Recoverable* ds){
        // invisible read doesn't need to verify epoch even if it's a
        // linearization point
        // this saves users from catching EpochVerifyException
        return load(ds);
    }

    // extern std::atomic<size_t> abort_cnt;
    // extern std::atomic<size_t> total_cnt;

    template<typename T>
    bool atomic_lin_var<T>::CAS_verify(Recoverable* ds, lin_var expected, const T& desired){
        assert(ds->_esys->epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        // total_cnt.fetch_add(1);
#ifdef USE_TSX
        unsigned status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            lin_var r = var.load();
            if(!r.is_desc()){
                if( r.cnt!=expected.cnt ||
                    r.val!=expected.val ||
                    !ds->check_epoch()){
                    _xend();
                    return false;
                } else {
                    lin_var new_r (reinterpret_cast<uint64_t>(desired), r.cnt+4);
                    var.store(new_r);
                    _xend();
                    return true;
                }
            } else {
                // we only help complete descriptor, but not retry
                _xend();
                r.get_desc()->try_complete(ds, reinterpret_cast<uint64_t>(this));
                return false;
            }
            // execution won't reach here; program should have returned
            assert(0);
        }
#endif
        // txn fails; fall back routine
        // abort_cnt.fetch_add(1);
        lin_var r = var.load();
        if(r.is_desc()){
            sc_desc_t* D = r.get_desc();
            D->try_complete(ds, reinterpret_cast<uint64_t>(this));
            return false;
        } else {
            if( r.cnt!=expected.cnt || 
                r.val!=expected.val) {
                return false;
            }
        }
        // now r.cnt must be ..00, and r.cnt+1 is ..01, which means "var
        // contains a descriptor" and "a descriptor is in progress"
        assert((r.cnt & 3UL) == 0UL);
        new (ds->get_dcss_desc()) sc_desc_t(r.cnt+1, 
                                    reinterpret_cast<uint64_t>(this), 
                                    expected.val, 
                                    reinterpret_cast<uint64_t>(desired), 
                                    ds->_esys->epochs[pds::EpochSys::tid].ui);
        lin_var new_r(reinterpret_cast<uint64_t>(ds->get_dcss_desc()), r.cnt+1);
        if(!var.compare_exchange_strong(r,new_r)){
            return false;
        }
        ds->get_dcss_desc()->try_complete(ds, reinterpret_cast<uint64_t>(this));
        if(ds->get_dcss_desc()->committed()) return true;
        else return false;
    }

    template<typename T>
    bool atomic_lin_var<T>::CAS(lin_var expected, const T& desired){
        // CAS doesn't check epoch; just cas ptr to desired, with cnt+=4
        assert(!expected.is_desc());
        lin_var new_r(reinterpret_cast<uint64_t>(desired), expected.cnt + 4);
        if(!var.compare_exchange_strong(expected,new_r)){
            return false;
        }
        return true;
    }

#endif /* !VISIBLE_READ */
} // namespace pds

#endif
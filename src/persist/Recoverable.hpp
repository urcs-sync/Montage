#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "pblk_naked.hpp"
// TODO: report recover errors/exceptions

class Recoverable{
    pds::EpochSys* _esys = nullptr;
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
        MontageOpHolder(): esys_(pds::esys){
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
        MontageOpHolderReadOnly(): esys_(pds::esys){
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
    std::unordered_map<uint64_t, pds::PBlk*>* recover(const int rec_thd=10){
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
t TOKEN_CONCAT(get_, n)() const{\
    return pds::esys->openread_pblk(this)->TOKEN_CONCAT(m_, n);\
}\
/* get method open a pblk for read. Allows old-see-new reads. */\
t TOKEN_CONCAT(get_unsafe_, n)(Recoverable* ds) const{\
    return ds->openread_pblk_unsafe(this)->TOKEN_CONCAT(m_, n);\
}\
t TOKEN_CONCAT(get_unsafe_, n)() const{\
    return pds::esys->openread_pblk_unsafe(this)->TOKEN_CONCAT(m_, n);\
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
template <class in_type>\
T* TOKEN_CONCAT(set_, n)(const in_type& TOKEN_CONCAT(tmp_, n)){\
    assert(pds::esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
    auto ret = pds::esys->openwrite_pblk(this);\
    ret->TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
    pds::esys->register_update_pblk(ret);\
    return ret;\
}\
/* set the field by the parameter. called only outside BEGIN_OP and END_OP */\
template <class in_type>\
void TOKEN_CONCAT(set_unsafe_, n)(Recoverable* ds, const in_type& TOKEN_CONCAT(tmp_, n)){\
    TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
}\
template <class in_type>\
void TOKEN_CONCAT(set_unsafe_, n)(const in_type& TOKEN_CONCAT(tmp_, n)){\
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
t TOKEN_CONCAT(get_, n)(int i) const{\
    return pds::esys->openread_pblk(this)->TOKEN_CONCAT(m_, n)[i];\
}\
/* get method open a pblk for read. Allows old-see-new reads. */\
t TOKEN_CONCAT(get_unsafe_, n)(Recoverable* ds, int i) const{\
    return ds->openread_pblk_unsafe(this)->TOKEN_CONCAT(m_, n)[i];\
}\
t TOKEN_CONCAT(get_unsafe_, n)(int i) const{\
    return pds::esys->openread_pblk_unsafe(this)->TOKEN_CONCAT(m_, n)[i];\
}\
/* set method open a pblk for write. return a new copy when necessary */\
T* TOKEN_CONCAT(set_, n)(Recoverable* ds, int i, t TOKEN_CONCAT(tmp_, n)){\
    assert(ds->epochs[EpochSys::tid].ui != NULL_EPOCH);\
    auto ret = ds->openwrite_pblk(this);\
    ret->TOKEN_CONCAT(m_, n)[i] = TOKEN_CONCAT(tmp_, n);\
    ds->register_update_pblk(ret);\
    return ret;\
}\
T* TOKEN_CONCAT(set_, n)(int i, t TOKEN_CONCAT(tmp_, n)){\
    assert(pds::esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
    auto ret = pds::esys->openwrite_pblk(this);\
    ret->TOKEN_CONCAT(m_, n)[i] = TOKEN_CONCAT(tmp_, n);\
    pds::esys->register_update_pblk(ret);\
    return ret;\
}


namespace pds{

    template<typename T>
    void atomic_nbptr_t<T>::store(const T& desired){
        // this function must be used only when there's no data race
        nbptr_t r = nbptr.load();
        nbptr_t new_r(reinterpret_cast<uint64_t>(desired),r.cnt);
        nbptr.store(new_r);
    }

#ifdef VISIBLE_READ
    // implementation of load and cas for visible reads

    template<typename T>
    nbptr_t atomic_nbptr_t<T>::load(){
        nbptr_t r;
        while(true){
            r = nbptr.load();
            nbptr_t ret(r.val,r.cnt+1);
            if(nbptr.compare_exchange_strong(r, ret))
                return ret;
        }
    }

    template<typename T>
    nbptr_t atomic_nbptr_t<T>::load_verify(){
        assert(pds::esys->epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        nbptr_t r;
        while(true){
            r = nbptr.load();
            if(pds::esys->check_epoch(pds::esys->epochs[pds::EpochSys::tid].ui)){
                nbptr_t ret(r.val,r.cnt+1);
                if(nbptr.compare_exchange_strong(r, ret)){
                    return r;
                }
            } else {
                throw EpochVerifyException();
            }
        }
    }

    template<typename T>
    bool atomic_nbptr_t<T>::CAS_verify(nbptr_t expected, const T& desired){
        assert(pds::esys->epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        if(pds::esys->check_epoch(pds::esys->epochs[pds::EpochSys::tid].ui)){
            nbptr_t new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
            return nbptr.compare_exchange_strong(expected, new_r);
        } else {
            return false;
        }
    }

    template<typename T>
    bool atomic_nbptr_t<T>::CAS(nbptr_t expected, const T& desired){
        nbptr_t new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
        return nbptr.compare_exchange_strong(expected, new_r);
    }

#else /* !VISIBLE_READ */
    /* implementation of load and cas for invisible reads */

    template<typename T>
    nbptr_t atomic_nbptr_t<T>::load(){
        nbptr_t r;
        do { 
            r = nbptr.load();
            if(r.is_desc()) {
                sc_desc_t* D = r.get_desc();
                D->try_complete(pds::esys, reinterpret_cast<uint64_t>(this));
            }
        } while(r.is_desc());
        return r;
    }

    template<typename T>
    nbptr_t atomic_nbptr_t<T>::load_verify(){
        // invisible read doesn't need to verify epoch even if it's a
        // linearization point
        // this saves users from catching EpochVerifyException
        return load();
    }

    // extern std::atomic<size_t> abort_cnt;
    // extern std::atomic<size_t> total_cnt;

    template<typename T>
    bool atomic_nbptr_t<T>::CAS_verify(nbptr_t expected, const T& desired){
        assert(pds::esys->epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        // total_cnt.fetch_add(1);
#ifdef USE_TSX
        unsigned status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            nbptr_t r = nbptr.load();
            if(!r.is_desc()){
                if( r.cnt!=expected.cnt ||
                    r.val!=expected.val ||
                    !pds::esys->check_epoch(pds::esys->epochs[pds::EpochSys::tid].ui)){
                    _xend();
                    return false;
                } else {
                    nbptr_t new_r (reinterpret_cast<uint64_t>(desired), r.cnt+4);
                    nbptr.store(new_r);
                    _xend();
                    return true;
                }
            } else {
                // we only help complete descriptor, but not retry
                _xend();
                r.get_desc()->try_complete(pds::esys, reinterpret_cast<uint64_t>(this));
                return false;
            }
            // execution won't reach here; program should have returned
            assert(0);
        }
#endif
        // txn fails; fall back routine
        // abort_cnt.fetch_add(1);
        nbptr_t r = nbptr.load();
        if(r.is_desc()){
            sc_desc_t* D = r.get_desc();
            D->try_complete(pds::esys, reinterpret_cast<uint64_t>(this));
            return false;
        } else {
            if( r.cnt!=expected.cnt || 
                r.val!=expected.val) {
                return false;
            }
        }
        // now r.cnt must be ..00, and r.cnt+1 is ..01, which means "nbptr
        // contains a descriptor" and "a descriptor is in progress"
        assert((r.cnt & 3UL) == 0UL);
        new (&pds::esys->local_descs[pds::EpochSys::tid].ui) sc_desc_t(r.cnt+1, 
                                    reinterpret_cast<uint64_t>(this), 
                                    expected.val, 
                                    reinterpret_cast<uint64_t>(desired), 
                                    pds::esys->epochs[pds::EpochSys::tid].ui);
        nbptr_t new_r(reinterpret_cast<uint64_t>(&pds::esys->local_descs[pds::EpochSys::tid].ui), r.cnt+1);
        if(!nbptr.compare_exchange_strong(r,new_r)){
            return false;
        }
        pds::esys->local_descs[pds::EpochSys::tid].ui.try_complete(pds::esys, reinterpret_cast<uint64_t>(this));
        if(pds::esys->local_descs[pds::EpochSys::tid].ui.committed()) return true;
        else return false;
    }

    template<typename T>
    bool atomic_nbptr_t<T>::CAS(nbptr_t expected, const T& desired){
        // CAS doesn't check epoch; just cas ptr to desired, with cnt+=4
        assert(!expected.is_desc());
        nbptr_t new_r(reinterpret_cast<uint64_t>(desired), expected.cnt + 4);
        if(!nbptr.compare_exchange_strong(expected,new_r)){
            return false;
        }
        return true;
    }

#endif /* !VISIBLE_READ */
} // namespace pds

#endif
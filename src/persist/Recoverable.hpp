#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "pblk_naked.hpp"
// TODO: report recover errors/exceptions

class Recoverable{
public:
    pds::EpochSys* _esys = nullptr;
    // return num of blocks recovered.
    virtual int recover(bool simulated = false) = 0;
    Recoverable(GlobalTestConfig* gtc);
    ~Recoverable();

    void init_thread(GlobalTestConfig*, LocalTestConfig* ltc);
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
    T* register_update_pblk(T* b){
        return _esys->register_update_pblk(b);
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


#endif
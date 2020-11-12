#ifndef PBLK_NAKED_HPP
#define PBLK_NAKED_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "Recoverable.hpp"
#include "ConcurrentPrimitives.hpp"

#include <typeinfo>

// This api is inspired by object-based RSTM's api.

namespace pds{

    extern EpochSys* esys;

    inline void init(GlobalTestConfig* gtc){
        // here we assume that pds::init is called before pds::init_thread, hence the assertion.
        // if this assertion triggers, note that the order may be reversed. Evaluation needed.
        assert(EpochSys::tid == -1);
        if (EpochSys::tid == -1){
            EpochSys::tid = 0;
        }
        esys = new EpochSys(gtc);
    }

    inline void init_thread(int id) {
        EpochSys::tid = id;
        // esys->init_thread(id);
    }

    inline void finalize(){
        delete esys;
        esys = nullptr; // for debugging.
    }

    #define CHECK_EPOCH() ({\
        esys->check_epoch(esys->epochs[EpochSys::tid].ui);\
    })

    // TODO: get rid of arguments in rideables.
    #define BEGIN_OP( ... ) ({ \
        esys->begin_op();})

    // end current operation by reducing transaction count of our epoch.
    // if our operation is already aborted, do nothing.
    #define END_OP ({\
        esys->end_op(); })

    // end current operation by reducing transaction count of our epoch.
    // if our operation is already aborted, do nothing.
    #define END_READONLY_OP ({\
        esys->end_readonly_op(); })

    // end current epoch and not move towards next epoch in esys.
    #define ABORT_OP ({ \
        esys->abort_op(); })

    class EpochHolder{
    public:
        ~EpochHolder(){
            END_OP;
        }
    };

    class EpochHolderReadOnly{
    public:
        ~EpochHolderReadOnly(){
            END_READONLY_OP;
        }
    };

    #define BEGIN_OP_AUTOEND( ... ) \
    BEGIN_OP();\
    EpochHolder __holder;

    #define BEGIN_READONLY_OP_AUTOEND( ... ) \
    BEGIN_OP();\
    EpochHolderReadOnly __holder;
    
    #define PNEW(t, ...) ({\
        esys->pnew<t>(__VA_ARGS__ );})

    #define PDELETE(b) ({\
        esys->pdelete(b);})

    #define PRETIRE(b) ({\
        esys->pretire(b);})

    #define PRECLAIM(b) ({\
        esys->preclaim(b);})

    // Hs: This is for "owned" PBlk's, currently not used in code base.
    // may be useful for "data" blocks like dynamically-sized
    // persistent String payload.
    #define PDELETE_DATA(b) ({\
        if (esys->sys_mode == ONLINE) {\
            delete(b);\
        }})



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
        assert(ds->_esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        return ds->_esys->openread_pblk(this, esys->epochs[EpochSys::tid].ui)->TOKEN_CONCAT(m_, n);\
    }\
    t TOKEN_CONCAT(get_, n)() const{\
        assert(esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        return esys->openread_pblk(this, esys->epochs[EpochSys::tid].ui)->TOKEN_CONCAT(m_, n);\
    }\
    /* get method open a pblk for read. Allows old-see-new reads. */\
    t TOKEN_CONCAT(get_unsafe_, n)(Recoverable* ds) const{\
        if(ds->_esys->epochs[EpochSys::tid].ui != NULL_EPOCH)\
            return ds->_esys->openread_pblk_unsafe(this, ds->_esys->epochs[EpochSys::tid].ui)->TOKEN_CONCAT(m_, n);\
        else\
            return TOKEN_CONCAT(m_, n);\
    }\
    t TOKEN_CONCAT(get_unsafe_, n)() const{\
        if(esys->epochs[EpochSys::tid].ui != NULL_EPOCH)\
            return esys->openread_pblk_unsafe(this, esys->epochs[EpochSys::tid].ui)->TOKEN_CONCAT(m_, n);\
        else\
            return TOKEN_CONCAT(m_, n);\
    }\
    /* set method open a pblk for write. return a new copy when necessary */\
    template <class in_type>\
    T* TOKEN_CONCAT(set_, n)(Recoverable* ds, const in_type& TOKEN_CONCAT(tmp_, n)){\
        assert(ds->_esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        auto ret = ds->_esys->openwrite_pblk(this, ds->_esys->epochs[EpochSys::tid].ui);\
        ret->TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
        ds->_esys->register_update_pblk(ret, ds->_esys->epochs[EpochSys::tid].ui);\
        return ret;\
    }\
    template <class in_type>\
    T* TOKEN_CONCAT(set_, n)(const in_type& TOKEN_CONCAT(tmp_, n)){\
        assert(esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        auto ret = esys->openwrite_pblk(this, esys->epochs[EpochSys::tid].ui);\
        ret->TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
        esys->register_update_pblk(ret, esys->epochs[EpochSys::tid].ui);\
        return ret;\
    }\
    /* set the field by the parameter. called only outside BEGIN_OP and END_OP */\
    template <class in_type>\
    void TOKEN_CONCAT(set_unsafe_, n)(Recoverable* ds, const in_type& TOKEN_CONCAT(tmp_, n)){\
        assert(ds->_esys->epochs[EpochSys::tid].ui == NULL_EPOCH);\
        TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
    }\
    template <class in_type>\
    void TOKEN_CONCAT(set_unsafe_, n)(const in_type& TOKEN_CONCAT(tmp_, n)){\
        assert(esys->epochs[EpochSys::tid].ui == NULL_EPOCH);\
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
        assert(ds->_esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        return ds->_esys->openread_pblk(this, ds->_esys->epochs[EpochSys::tid].ui)->TOKEN_CONCAT(m_, n)[i];\
    }\
    t TOKEN_CONCAT(get_, n)(int i) const{\
        assert(esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        return esys->openread_pblk(this, esys->epochs[EpochSys::tid].ui)->TOKEN_CONCAT(m_, n)[i];\
    }\
    /* get method open a pblk for read. Allows old-see-new reads. */\
    t TOKEN_CONCAT(get_unsafe_, n)(Recoverable* ds, int i) const{\
        assert(ds->_esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        return ds->_esys->openread_pblk_unsafe(this, ds->_esys->epochs[EpochSys::tid].ui)->TOKEN_CONCAT(m_, n)[i];\
    }\
    t TOKEN_CONCAT(get_unsafe_, n)(int i) const{\
        assert(esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        return esys->openread_pblk_unsafe(this, esys->epochs[EpochSys::tid].ui)->TOKEN_CONCAT(m_, n)[i];\
    }\
    /* set method open a pblk for write. return a new copy when necessary */\
    T* TOKEN_CONCAT(set_, n)(Recoverable* ds, int i, t TOKEN_CONCAT(tmp_, n)){\
        assert(ds->_esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        auto ret = ds->_esys->openwrite_pblk(this, ds->_esys->epochs[EpochSys::tid].ui);\
        ret->TOKEN_CONCAT(m_, n)[i] = TOKEN_CONCAT(tmp_, n);\
        ds->_esys->register_update_pblk(ret, ds->_esys->epochs[EpochSys::tid].ui);\
        return ret;\
    }\
    T* TOKEN_CONCAT(set_, n)(int i, t TOKEN_CONCAT(tmp_, n)){\
        assert(esys->epochs[EpochSys::tid].ui != NULL_EPOCH);\
        auto ret = esys->openwrite_pblk(this, esys->epochs[EpochSys::tid].ui);\
        ret->TOKEN_CONCAT(m_, n)[i] = TOKEN_CONCAT(tmp_, n);\
        esys->register_update_pblk(ret, esys->epochs[EpochSys::tid].ui);\
        return ret;\
    }

    inline std::unordered_map<uint64_t, PBlk*>* recover(const int rec_thd=10){
        return esys->recover(rec_thd);
    }

    inline void flush(){
        esys->flush();
    }

    inline void recover_mode(){
        esys->recover_mode();
    }

    inline void online_mode(){
        esys->online_mode();
    }
}
#endif

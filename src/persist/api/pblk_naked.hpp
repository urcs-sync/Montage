#ifndef PBLK_NAKED_HPP
#define PBLK_NAKED_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "ConcurrentPrimitives.hpp"
#include "LLSC.hpp"
#include <typeinfo>

// This api is inspired by object-based RSTM's api.

namespace pds{

    extern EpochSys* esys;
    extern padded<uint64_t>* epochs;
    extern __thread int _tid;
    extern padded<sc_desc_t>* local_descs;

    inline void init(GlobalTestConfig* gtc){
        // here we assume that pds::init is called before pds::init_thread, hence the assertion.
        // if this assertion triggers, note that the order may be reversed. Evaluation needed.
        assert(_tid == -1);
        if (_tid == -1){
            _tid = 0;
        }
        sys_mode = ONLINE;
        PBlk::init(gtc->task_num);
        epochs = new padded<uint64_t>[gtc->task_num];
        local_descs = new padded<sc_desc_t>[gtc->task_num];
        for(int i = 0; i < gtc->task_num; i++){
            epochs[i].ui = NULL_EPOCH;
        }
        esys = new EpochSys(gtc);
    }

    inline void init_thread(int id) {
        _tid = id;
        // esys->init_thread(id);
    }

    inline void finalize(){
        delete esys;
    }

    #define CHECK_EPOCH() ({\
        esys->check_epoch(epochs[_tid].ui);\
    })

    #define BEGIN_OP( ... ) ({ \
    assert(epochs[_tid].ui == NULL_EPOCH);\
    epochs[_tid].ui = esys->begin_transaction();\
    std::vector<PBlk*> __blks = { __VA_ARGS__ };\
    for (auto b = __blks.begin(); b != __blks.end(); b++){\
        esys->register_alloc_pblk(*b, epochs[_tid].ui);\
    }\
    assert(epochs[_tid].ui != NULL_EPOCH); })

    // end current operation by reducing transaction count of our epoch.
    // if our operation is already aborted, do nothing.
    #define END_OP ({\
    if (epochs[_tid].ui != NULL_EPOCH){ \
        esys->end_transaction(epochs[_tid].ui);\
        epochs[_tid].ui = NULL_EPOCH;} })

    // end current operation by reducing transaction count of our epoch.
    // if our operation is already aborted, do nothing.
    #define END_READONLY_OP ({\
    if (epochs[_tid].ui != NULL_EPOCH){ \
        esys->end_readonly_transaction(epochs[_tid].ui);\
        epochs[_tid].ui = NULL_EPOCH;} })

    // end current epoch and not move towards next epoch in esys.
    #define ABORT_OP ({ \
    assert(epochs[_tid].ui != NULL_EPOCH);\
    esys->abort_transaction(epochs[_tid].ui);\
    epochs[_tid].ui = NULL_EPOCH;})


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
    BEGIN_OP( __VA_ARGS__ );\
    EpochHolder __holder;

    #define BEGIN_READONLY_OP_AUTOEND( ... ) \
    BEGIN_OP( __VA_ARGS__ );\
    EpochHolderReadOnly __holder;
    
    #define PNEW(t, ...) ({\
    epochs[_tid].ui == NULL_EPOCH ? \
        new t( __VA_ARGS__ ) : \
        esys->register_alloc_pblk(new t( __VA_ARGS__ ), epochs[_tid].ui);})

    #define PDELETE(b) ({\
    if (sys_mode == ONLINE) {\
    assert(epochs[_tid].ui != NULL_EPOCH);\
    esys->free_pblk(b, epochs[_tid].ui);}})

    #define PDELETE_DATA(b) ({\
        if (sys_mode == ONLINE) {\
            delete(b);\
        }})

    #define PRETIRE(b) ({\
    assert(epochs[_tid].ui != NULL_EPOCH);\
    esys->retire_pblk(b, epochs[_tid].ui);\
    })

    #define PRECLAIM(b) ({\
    if (epochs[_tid].ui == NULL_EPOCH){\
        BEGIN_OP_AUTOEND();\
        esys->reclaim_pblk(b, epochs[_tid].ui);\
    } else {\
        esys->reclaim_pblk(b, epochs[_tid].ui);\
    }\
    })

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
    t TOKEN_CONCAT(get_, n)() const{\
        assert(epochs[_tid].ui != NULL_EPOCH);\
        return esys->openread_pblk(this, epochs[_tid].ui)->TOKEN_CONCAT(m_, n);\
    }\
    /* get method open a pblk for read. Allows old-see-new reads. */\
    t TOKEN_CONCAT(get_unsafe_, n)() const{\
        if(epochs[_tid].ui != NULL_EPOCH)\
            return esys->openread_pblk_unsafe(this, epochs[_tid].ui)->TOKEN_CONCAT(m_, n);\
        else\
            return TOKEN_CONCAT(m_, n);\
    }\
    /* set method open a pblk for write. return a new copy when necessary */\
    template <class in_type>\
    T* TOKEN_CONCAT(set_, n)(const in_type& TOKEN_CONCAT(tmp_, n)){\
        assert(epochs[_tid].ui != NULL_EPOCH);\
        auto ret = esys->openwrite_pblk(this, epochs[_tid].ui);\
        ret->TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
        esys->register_update_pblk(ret, epochs[_tid].ui);\
        return ret;\
    }\
    /* set the field by the parameter. called only outside BEGIN_OP and END_OP */\
    template <class in_type>\
    void TOKEN_CONCAT(set_unsafe_, n)(const in_type& TOKEN_CONCAT(tmp_, n)){\
        assert(epochs[_tid].ui == NULL_EPOCH);\
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
    t TOKEN_CONCAT(get_, n)(int i) const{\
        assert(epochs[_tid].ui != NULL_EPOCH);\
        return esys->openread_pblk(this, epochs[_tid].ui)->TOKEN_CONCAT(m_, n)[i];\
    }\
    /* get method open a pblk for read. Allows old-see-new reads. */\
    t TOKEN_CONCAT(get_unsafe_, n)(int i) const{\
        assert(epochs[_tid].ui != NULL_EPOCH);\
        return esys->openread_pblk_unsafe(this, epochs[_tid].ui)->TOKEN_CONCAT(m_, n)[i];\
    }\
    /* set method open a pblk for write. return a new copy when necessary */\
    T* TOKEN_CONCAT(set_, n)(int i, t TOKEN_CONCAT(tmp_, n)){\
        assert(epochs[_tid].ui != NULL_EPOCH);\
        auto ret = esys->openwrite_pblk(this, epochs[_tid].ui);\
        ret->TOKEN_CONCAT(m_, n)[i] = TOKEN_CONCAT(tmp_, n);\
        esys->register_update_pblk(ret, epochs[_tid].ui);\
        return ret;\
    }

    inline std::unordered_map<uint64_t, PBlk*>* recover(const int rec_thd=10){
        return esys->recover(rec_thd);
    }

    inline void flush(){
        esys->flush();
    }

    inline void recover_mode(){
        pds::sys_mode = RECOVER;
    }

    inline void online_mode(){
        pds::sys_mode = ONLINE;
    }

    // class PBlk : public PBlkBase{
    //     friend class EpochSys;
    // public:
    //     PBlk():PBlkBase(false){}
    //     PBlk(const PBlk& oth):PBlkBase(oth){}
    //     virtual ~PBlk() {}
    // };

    // class PData : public PBlk{
    //     friend class EpochSys;
    // public:
    //     PData():PBlk(true) {}
    //     PData(const PData& oth):PBlk(oth){}
    //     virtual ~PData() {}

    //     template<typename T>
    //     static T* alloc(size_t s, uint64_t head_id){
    //         assert(epochs[_tid].ui != NULL_EPOCH);
    //         T* ret = static_cast<T*>(RP_malloc(sizeof(T) + s));
    //         new (ret) T(data, s);
    //         esys->register_alloc_pdata(ret, epochs[_tid].ui, head_id);
    //         return ret;
    //     }
    // };

}
#endif

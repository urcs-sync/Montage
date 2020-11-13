#ifndef PERSIST_STRUCT_API_HPP
#define PERSIST_STRUCT_API_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "Recoverable.hpp"
#include "ConcurrentPrimitives.hpp"

#include <typeinfo>

// This api is inspired by object-based RSTM's api.

namespace pds{

    extern EpochSys* global_esys;

    inline void init(GlobalTestConfig* gtc){
        // here we assume that pds::init is called before pds::init_thread, hence the assertion.
        // if this assertion triggers, note that the order may be reversed. Evaluation needed.
        assert(EpochSys::tid == -1);
        if (EpochSys::tid == -1){
            EpochSys::tid = 0;
        }
        global_esys = new EpochSys(gtc);
    }

    inline void init_thread(int id) {
        EpochSys::tid = id;
        // global_esys->init_thread(id);
    }

    inline void finalize(){
        delete global_esys;
        global_esys = nullptr; // for debugging.
    }

    #define CHECK_EPOCH() ({\
        global_esys->check_epoch();})

    // TODO: get rid of arguments in rideables.
    #define BEGIN_OP( ... ) ({ \
        global_esys->begin_op();})

    // end current operation by reducing transaction count of our epoch.
    // if our operation is already aborted, do nothing.
    #define END_OP ({\
        global_esys->end_op(); })

    // end current operation by reducing transaction count of our epoch.
    // if our operation is already aborted, do nothing.
    #define END_READONLY_OP ({\
        global_esys->end_readonly_op(); })

    // end current epoch and not move towards next epoch in global_esys.
    #define ABORT_OP ({ \
        global_esys->abort_op(); })

    #define BEGIN_OP_AUTOEND( ... ) \
        Recoverable::MontageOpHolder __holder;

    #define BEGIN_READONLY_OP_AUTOEND( ... ) \
        Recoverable::MontageOpHolderReadOnly __holder;
    
    #define PNEW(t, ...) ({\
        global_esys->register_alloc_pblk(new t(__VA_ARGS__));})

    #define PDELETE(b) ({\
        global_esys->pdelete(b);})

    #define PRETIRE(b) ({\
        global_esys->pretire(b);})

    #define PRECLAIM(b) ({\
        global_esys->preclaim(b);})

    // Hs: This is for "owned" PBlk's, currently not used in code base.
    // may be useful for "data" blocks like dynamically-sized
    // persistent String payload.
    #define PDELETE_DATA(b) ({\
        if (global_esys->sys_mode == ONLINE) {\
            delete(b);\
        }})

    inline std::unordered_map<uint64_t, PBlk*>* recover(const int rec_thd=10){
        return global_esys->recover(rec_thd);
    }

    inline void flush(){
        global_esys->flush();
    }

    inline void recover_mode(){
        global_esys->recover_mode();
    }

    inline void online_mode(){
        global_esys->online_mode();
    }
}

#endif
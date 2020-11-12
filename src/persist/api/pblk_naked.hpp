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
        esys->check_epoch();})

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

    #define BEGIN_OP_AUTOEND( ... ) \
        Recoverable::MontageOpHolder __holder;

    #define BEGIN_READONLY_OP_AUTOEND( ... ) \
        Recoverable::MontageOpHolderReadOnly __holder;
    
    #define PNEW(t, ...) ({\
        esys->register_alloc_pblk(new t(__VA_ARGS__));})

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

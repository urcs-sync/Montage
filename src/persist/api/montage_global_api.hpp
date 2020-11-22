#ifndef PERSIST_STRUCT_API_HPP
#define PERSIST_STRUCT_API_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "Recoverable.hpp"
#include "ConcurrentPrimitives.hpp"

#include <typeinfo>

namespace pds{
    class GlobalRecoverable: public Recoverable{
        std::unordered_map<uint64_t, PBlk*>* recovered_pblks = nullptr;
    public:
        GlobalRecoverable(GlobalTestConfig* gtc): Recoverable(gtc){}
        ~GlobalRecoverable(){
            if (recovered_pblks){
                delete recovered_pblks;
            }
        }
        int recover(bool simulated){
            // TODO: handle simulated situation here?
            recovered_pblks = recover_pblks();
            // TODO: return number of blocks here?
            return 0;
        }
        std::unordered_map<uint64_t, PBlk*>* get_recovered(){
            return recovered_pblks;
        }
    };

    extern GlobalRecoverable* global_recoverable;
    
    inline void init(GlobalTestConfig* gtc){
        // here we assume that pds::init is called before pds::init_thread, hence the assertion.
        // if this assertion triggers, note that the order may be reversed. Evaluation needed.
        assert(EpochSys::tid == -1);
        if (EpochSys::tid == -1){
            EpochSys::tid = 0;
        }
        global_recoverable = new GlobalRecoverable(gtc);
    }

    inline void init_thread(int id) {
        EpochSys::tid = id;
        // esys_global->init_thread(id);
    }

    inline void finalize(){
        delete global_recoverable;
        global_recoverable = nullptr; // for debugging.
    }

    #define CHECK_CURR_OP_EPOCH() ({\
        global_recoverable->check_epoch();})

    #define CHECK_EPOCH(c) ({\
        global_recoverable->check_epoch(c);})

    // TODO: get rid of arguments in rideables.
    #define BEGIN_OP( ... ) ({ \
        global_recoverable->begin_op();})

    // end current operation by reducing transaction count of our epoch.
    // if our operation is already aborted, do nothing.
    #define END_OP ({\
        global_recoverable->end_op();})

    // end current operation by reducing transaction count of our epoch.
    // if our operation is already aborted, do nothing.
    #define END_READONLY_OP ({\
        global_recoverable->end_readonly_op();})

    // end current epoch and not move towards next epoch in global_recoverable.
    #define ABORT_OP ({ \
        global_recoverable->abort_op();})

    #define BEGIN_OP_AUTOEND( ... ) \
        Recoverable::MontageOpHolder __holder(global_recoverable);

    #define BEGIN_READONLY_OP_AUTOEND( ... ) \
        Recoverable::MontageOpHolderReadOnly __holder_readonly(global_recoverable);
    
    #define PNEW(t, ...) ({\
        global_recoverable->pnew<t>(__VA_ARGS__));})

    #define PDELETE(b) ({\
        global_recoverable->pdelete(b);})

    #define PRETIRE(b) ({\
        global_recoverable->pretire(b);})

    #define PRECLAIM(b) ({\
        global_recoverable->preclaim(b);})
    
    #define POPEN_READ(b) ({\
        global_recoverable->openread_pblk(b);})
    
    #define POPEN_UNSAFE_READ(b) ({\
        global_recoverable->openread_pblk_usnafe(b);})
    
    #define POPEN_WRITE(b) ({\
        global_recoverable->openwrite_pblk(b);})
    
    #define REGISTER_PUPDATE(b) ({\
        global_recoverable->register_update_pblk(b);})

    // Hs: This is for "owned" PBlk's, currently not used in code base.
    // may be useful for "data" blocks like dynamically-sized
    // persistent String payload.
    // #define PDELETE_DATA(b) ({
    //     if (global_recoverable->sys_mode == ONLINE) {
    //         delete(b);
    //     }})

    inline std::unordered_map<uint64_t, PBlk*>* recover(const int rec_thd=10){
        global_recoverable->recover(rec_thd);
        return global_recoverable->get_recovered();
    }

    inline void flush(){
        global_recoverable->flush();
    }

    inline void recover_mode(){
        global_recoverable->recover_mode();
    }

    inline void online_mode(){
        global_recoverable->online_mode();
    }
}

#endif
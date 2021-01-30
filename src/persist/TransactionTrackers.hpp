#ifndef TRANSACTION_TRACKERS
#define TRANSACTION_TRACKERS

#include <atomic>
#include <cstdint>

#include "persist_utils.hpp"
#include "ConcurrentPrimitives.hpp"

namespace pds{

    //////////////////////////
    // Transaction Trackers //
    //////////////////////////

    class TransactionTracker{
    public:
        std::atomic<uint64_t>* global_epoch = nullptr;
        TransactionTracker(std::atomic<uint64_t>* ge): global_epoch(ge){}
        virtual bool consistent_register_active(uint64_t target, uint64_t c) = 0;
        virtual bool consistent_register_bookkeeping(uint64_t target, uint64_t c) = 0;
        virtual void unregister_active(uint64_t target) = 0;
        virtual void unregister_bookkeeping(uint64_t target) = 0;
        virtual bool no_active(uint64_t target) = 0;
        virtual bool no_bookkeeping(uint64_t target) = 0;
        virtual void finalize(){}
        virtual ~TransactionTracker(){}
    };

    class PerEpochTransactionTracker: public TransactionTracker{
        paddedAtomic<uint64_t>* curr_epochs;
        int task_num;
        bool consistent_set(uint64_t target, uint64_t c);
    public:
        PerEpochTransactionTracker(std::atomic<uint64_t>* ge, int tn);
        bool consistent_register_active(uint64_t target, uint64_t c);
        bool consistent_register_bookkeeping(uint64_t target, uint64_t c);
        void unregister_active(uint64_t target);
        void unregister_bookkeeping(uint64_t target);
        bool no_active(uint64_t target);
        bool no_bookkeeping(uint64_t target);
        void finalize();
    };

    class NoTransactionTracker : public TransactionTracker{
        // a transaction counter that does absolutely nothing.
    public:
        NoTransactionTracker(std::atomic<uint64_t>* ge): TransactionTracker(ge){}
        bool consistent_register_active(uint64_t target, uint64_t c){
            return true;
        }
        bool consistent_register_bookkeeping(uint64_t target, uint64_t c){
            return true;
        }
        void unregister_active(uint64_t target){}
        void unregister_bookkeeping(uint64_t target){}
        bool no_active(uint64_t target){
            return true;
        }
        bool no_bookkeeping(uint64_t target){
            return true;
        }
    };

    class AtomicTransactionTracker : public TransactionTracker{
        paddedAtomic<uint64_t> active_transactions[EPOCH_WINDOW];
        paddedAtomic<uint64_t> bookkeeping_transactions[EPOCH_WINDOW];
        bool consistent_increment(std::atomic<uint64_t>& counter, const uint64_t c);
    public:
        AtomicTransactionTracker(std::atomic<uint64_t>* ge);
        bool consistent_register_active(uint64_t target, uint64_t c);
        bool consistent_register_bookkeeping(uint64_t target, uint64_t c);
        void unregister_active(uint64_t target);
        void unregister_bookkeeping(uint64_t target);
        bool no_active(uint64_t target);
        bool no_bookkeeping(uint64_t target);
    };

    class NoFenceTransactionTracker : public TransactionTracker{
        padded<paddedAtomic<bool>*> active_transactions[EPOCH_WINDOW];
        padded<paddedAtomic<bool>*> bookkeeping_transactions[EPOCH_WINDOW];
        int task_num;
        virtual void set_register(paddedAtomic<bool>* indicators);
        virtual void set_unregister(paddedAtomic<bool>* indicators);
        bool consistent_register(paddedAtomic<bool>* indicators, const uint64_t c);
        bool all_false(paddedAtomic<bool>* indicators);
    public:
        NoFenceTransactionTracker(std::atomic<uint64_t>* ge, int tn);
        bool consistent_register_active(uint64_t target, uint64_t c);
        bool consistent_register_bookkeeping(uint64_t target, uint64_t c);
        virtual void unregister_active(uint64_t target);
        virtual void unregister_bookkeeping(uint64_t target);
        bool no_active(uint64_t target);
        bool no_bookkeeping(uint64_t target);
    };

    class FenceBeginTransactionTracker : public NoFenceTransactionTracker{
        virtual void set_register(paddedAtomic<bool>* indicators);
    public:
        FenceBeginTransactionTracker(std::atomic<uint64_t>* ge, int task_num);
    };

    class FenceEndTransactionTracker : public NoFenceTransactionTracker{
        virtual void set_unregister(paddedAtomic<bool>* indicators);
    public:
        FenceEndTransactionTracker(std::atomic<uint64_t>* ge, int task_num);
    };

}
#endif
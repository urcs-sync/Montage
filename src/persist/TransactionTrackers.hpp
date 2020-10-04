#ifndef TRANSACTION_TRACKERS
#define TRANSACTION_TRACKERS

    //////////////////////////
    // Transaction Trackers //
    //////////////////////////

    class TransactionTracker{
    public:
        atomic<uint64_t>* global_epoch = nullptr;
        TransactionTracker(atomic<uint64_t>* ge): global_epoch(ge){}
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
        bool consistent_set(uint64_t target, uint64_t c){
            assert(_tid != -1);
            curr_epochs[_tid].ui.store(target, std::memory_order_seq_cst); // fence
            if (c == global_epoch->load(std::memory_order_acquire)){
                return true;
            } else {
                curr_epochs[_tid].ui.store(NULL_EPOCH, std::memory_order_seq_cst); // TODO: double-check this fence.
                return false;
            }
        }
    public:
        PerEpochTransactionTracker(atomic<uint64_t>* ge, int tn): TransactionTracker(ge), task_num(tn){
            curr_epochs = new paddedAtomic<uint64_t>[task_num];
            for (int i = 0; i < task_num; i++){
                curr_epochs[i].ui.store(NULL_EPOCH);
            }
        }
        bool consistent_register_active(uint64_t target, uint64_t c){
            return consistent_set(target, c);
        }
        bool consistent_register_bookkeeping(uint64_t target, uint64_t c){
            return consistent_set(target, c);
        }
        void unregister_active(uint64_t target){
            assert(_tid != -1);
            curr_epochs[_tid].ui.store(NULL_EPOCH, std::memory_order_seq_cst);
        }
        void unregister_bookkeeping(uint64_t target){
            assert(_tid != -1);
            curr_epochs[_tid].ui.store(NULL_EPOCH, std::memory_order_seq_cst);
        }
        bool no_active(uint64_t target){
            for (int i = 0; i < task_num; i++){
                uint64_t curr_epoch = curr_epochs[i].ui.load(std::memory_order_acquire);
                if (target == curr_epoch && curr_epoch != NULL_EPOCH){
                    // std::cout<<"target:"<<target<<" curr_epoch:"<<curr_epoch<<" i:"<<i<<std::endl;
                    return false;
                }
            }
            return true;
        }
        bool no_bookkeeping(uint64_t target){
            return no_active(target);
        }
        void finalize(){
            for (int i = 0; i < task_num; i++){
                curr_epochs[i].ui.store(NULL_EPOCH);
            }
        }
    };

    class NoTransactionTracker : public TransactionTracker{
        // a transaction counter that does absolutely nothing.
    public:
        NoTransactionTracker(atomic<uint64_t>* ge): TransactionTracker(ge){}
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
        paddedAtomic<uint64_t> active_transactions[4];
        paddedAtomic<uint64_t> bookkeeping_transactions[4];
        bool consistent_increment(std::atomic<uint64_t>& counter, const uint64_t c){
            counter.fetch_add(1, std::memory_order_seq_cst);
            if (c == global_epoch->load(std::memory_order_seq_cst)){
                return true;
            } else {
                counter.fetch_sub(1, std::memory_order_seq_cst);
                return false;
            }
        }
    public:
        AtomicTransactionTracker(atomic<uint64_t>* ge): TransactionTracker(ge){
            for (int i = 0; i < 4; i++){
                active_transactions[i].ui.store(0, std::memory_order_relaxed);
                bookkeeping_transactions[i].ui.store(0, std::memory_order_relaxed);
            }
        }
        bool consistent_register_active(uint64_t target, uint64_t c){
            return consistent_increment(active_transactions[target%4].ui, c);
        }
        bool consistent_register_bookkeeping(uint64_t target, uint64_t c){
            return consistent_increment(bookkeeping_transactions[target%4].ui, c);
        }
        void unregister_active(uint64_t target){
            active_transactions[target%4].ui.fetch_sub(1, std::memory_order_seq_cst);
        }
        void unregister_bookkeeping(uint64_t target){
            bookkeeping_transactions[target%4].ui.fetch_sub(1, std::memory_order_seq_cst);
        }
        bool no_active(uint64_t target){
            return (active_transactions[target%4].ui.load(std::memory_order_seq_cst) == 0);
        }
        bool no_bookkeeping(uint64_t target){
            return (bookkeeping_transactions[target%4].ui.load(std::memory_order_seq_cst) == 0);
        }
    };

    class NoFenceTransactionTracker : public TransactionTracker{
        padded<paddedAtomic<bool>*> active_transactions[4];
        padded<paddedAtomic<bool>*> bookkeeping_transactions[4];
        int task_num;
        virtual void set_register(paddedAtomic<bool>* indicators){
            assert(_tid != -1);
            indicators[_tid].ui.store(true, std::memory_order_release);
        }
        virtual void set_unregister(paddedAtomic<bool>* indicators){
            assert(_tid != -1);
            indicators[_tid].ui.store(false, std::memory_order_release);
        }
        bool consistent_register(paddedAtomic<bool>* indicators, const uint64_t c){
            set_register(indicators);
            if (c == global_epoch->load(std::memory_order_acquire)){
                return true;
            } else {
                // Hs: I guess we don't ever need a fence here.
                assert(_tid != -1);
                indicators[_tid].ui.store(false, std::memory_order_release);
                return false;
            }
        }
        bool all_false(paddedAtomic<bool>* indicators){
            for (int i = 0; i < task_num; i++){
                if (indicators[i].ui.load(std::memory_order_acquire) == true){
                    return false;
                }
            }
            return true;
        }
    public:
        NoFenceTransactionTracker(atomic<uint64_t>* ge, int tn): TransactionTracker(ge), task_num(tn){
            for (int i = 0; i < 4; i++){
                active_transactions[i].ui = new paddedAtomic<bool>[task_num];
                bookkeeping_transactions[i].ui = new paddedAtomic<bool>[task_num];
                for (int j = 0; j < task_num; j++){
                    active_transactions[i].ui[j].ui.store(false);
                    bookkeeping_transactions[i].ui[j].ui.store(false);
                }
            }
        }
        bool consistent_register_active(uint64_t target, uint64_t c){
            return consistent_register(active_transactions[target%4].ui, c);
        }
        bool consistent_register_bookkeeping(uint64_t target, uint64_t c){
            return consistent_register(bookkeeping_transactions[target%4].ui, c);
        }
        virtual void unregister_active(uint64_t target){
            set_unregister(active_transactions[target%4].ui);
        }
        virtual void unregister_bookkeeping(uint64_t target){
            set_unregister(bookkeeping_transactions[target%4].ui);
        }
        bool no_active(uint64_t target){
            return all_false(active_transactions[target%4].ui);
        }
        bool no_bookkeeping(uint64_t target){
            return all_false(bookkeeping_transactions[target%4].ui);
        }
    };

    class FenceBeginTransactionTracker : public NoFenceTransactionTracker{
        virtual void set_register(paddedAtomic<bool>* indicators){
            assert(_tid != -1);
            indicators[_tid].ui.store(true, std::memory_order_seq_cst);
        }
    public:
        FenceBeginTransactionTracker(atomic<uint64_t>* ge, int task_num): NoFenceTransactionTracker(ge, task_num){}
    };

    class FenceEndTransactionTracker : public NoFenceTransactionTracker{
        virtual void set_unregister(paddedAtomic<bool>* indicators){
            assert(_tid != -1);
            indicators[_tid].ui.store(false, std::memory_order_seq_cst);
        }
    public:
        FenceEndTransactionTracker(atomic<uint64_t>* ge, int task_num): NoFenceTransactionTracker(ge, task_num){}
    };

#endif
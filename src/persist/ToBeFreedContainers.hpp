#ifndef TO_BE_FREED_CONTAINERS_HPP
#define TO_BE_FREED_CONTAINERS_HPP

///////////////////////////
// To-be-free Containers //
///////////////////////////

    class ToBeFreedContainer{
    public:
        virtual void register_free(PBlk* blk, uint64_t c) {};
        virtual void help_free(uint64_t c) {};
        virtual void help_free_local(uint64_t c) {};
        virtual void clear() = 0;
        virtual void free_on_new_epoch(uint64_t c){};
        virtual ~ToBeFreedContainer(){}
    };

    class PerThreadFreedContainer : public ToBeFreedContainer{
        PerThreadContainer<PBlk*>* container = nullptr;
        padded<uint64_t>* threadEpoch;
        padded<std::mutex>* locks = nullptr;
        int task_num;
        static void do_free(PBlk*& x){
            delete x;
        }
    public:
        PerThreadFreedContainer(){}
        PerThreadFreedContainer(GlobalTestConfig* gtc): task_num(gtc->task_num){
            container = new VectorContainer<PBlk*>(gtc->task_num);
            threadEpoch = new padded<uint64_t>[gtc->task_num];
            locks = new padded<std::mutex>[gtc->task_num];
            for(int i = 0; i < gtc->task_num; i++){
                threadEpoch[i] = NULL_EPOCH;
            }
        }
        ~PerThreadFreedContainer(){
            delete container;
        }

        void free_on_new_epoch(uint64_t c){
            /* there are 3 possilibities:
                1. thread's previous transaction epoch is c, in this case, just return
                2. thread's previous transaction epoch is c-1, in this case, free the retired blocks in epoch c-2, and update the thread's
                   most recent transaction epoch number
                3. thread's previous transaction epoch is smaller than c-1, in this case, just return, because epoch advanver has already
                   freed all the blocks from 2 epochs ago, then update the thread's most recent transaction epoch number
                So we need to keep the to_be_free->help_free(c-2) in epoch_advancer. */

            if( c == threadEpoch[_tid] -1){
                std::lock_guard<std::mutex> lk(locks[_tid].ui);
                help_free_local(c - 2);
                threadEpoch[_tid] = c;
            }else if( c < threadEpoch[_tid] -1){
                threadEpoch[_tid] = c;
            }
        }

        void register_free(PBlk* blk, uint64_t c){
            // container[c%4].ui->push(blk, _tid);
            container->push(blk, c);
        }
        void help_free(uint64_t c){
            // try to get all the locks, spin when unable to get the target lock while holding all acquired locks
            // optimization?
            for(int i = 0; i < task_num; i++){
                while(!locks[i].ui.try_lock()){}
            }

            container->pop_all(&do_free, c);
            
            for(int i = 0; i < task_num; i++){
                locks[i].ui.unlock();
            }
        }
        void help_free_local(uint64_t c){
            container->pop_all_local(&do_free, c);
        }
        void clear(){
            container->clear();
        }
    };

    class PerEpochFreedContainer : public ToBeFreedContainer{
        PerThreadContainer<PBlk*>* container = nullptr;
        static void do_free(PBlk*& x){
            delete x;
        }
    public:
        PerEpochFreedContainer(){
            // errexit("DO NOT USE DEFAULT CONSTRUCTOR OF ToBeFreedContainer");
        }
        PerEpochFreedContainer(GlobalTestConfig* gtc){
            container = new VectorContainer<PBlk*>(gtc->task_num);
            // container = new HashSetContainer<PBlk*>(gtc->task_num);
        }
        ~PerEpochFreedContainer(){
            delete container;
        }
        void free_on_new_epoch(uint64_t c){}
        void register_free(PBlk* blk, uint64_t c){
            // container[c%4].ui->push(blk, _tid);
            container->push(blk, c);
        }
        void help_free(uint64_t c){
            container->pop_all(&do_free, c);
        }
        void help_free_local(uint64_t c){
            container->pop_all_local(&do_free, c);
        }
        void clear(){
            container->clear();
        }
    };

    class NoToBeFreedContainer : public ToBeFreedContainer{
        // A to-be-freed container that does absolutely nothing.
    public:
        NoToBeFreedContainer(){}
        virtual void register_free(PBlk* blk, uint64_t c){
            delete blk;
        }
        void free_on_new_epoch(uint64_t c){}
        virtual void help_free(uint64_t c){}
        virtual void help_free_local(uint64_t c){}
        virtual void clear(){}
    };

    struct Epoch : public PBlk{
        std::atomic<uint64_t> global_epoch;
        void persist(){}
        Epoch(){
            global_epoch.store(NULL_EPOCH, std::memory_order_relaxed);
        }
    };

#endif

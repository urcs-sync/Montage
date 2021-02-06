#include "EpochSys.hpp"

#include <omp.h>
#include <atomic>
namespace pds{

    thread_local int EpochSys::tid = -1;
    std::atomic<int> EpochSys::esys_num(0);
    void EpochSys::parse_env(){
        if (to_be_persisted){
            delete to_be_persisted;
        }
        if (to_be_freed){
            delete to_be_freed;
        }
        if (epoch_advancer){
            delete epoch_advancer;
        }
        if (trans_tracker){
            delete trans_tracker;
        }

    if (!gtc->checkEnv("EpochLengthUnit")){
        gtc->setEnv("EpochLengthUnit", "Millisecond");
    }

    if (!gtc->checkEnv("EpochLength")){
        gtc->setEnv("EpochLength", "50");
    }

    if (!gtc->checkEnv("BufferSize")){
        gtc->setEnv("BufferSize", "64");
    }

        if (gtc->checkEnv("PersistStrat")){
            if (gtc->getEnv("PersistStrat") == "No"){
                to_be_persisted = new NoToBePersistContainer();
                to_be_freed = new NoToBeFreedContainer(this);
                epoch_advancer = new NoEpochAdvancer();
                trans_tracker = new NoTransactionTracker(this->global_epoch);
                return;
            }
        }

        if (gtc->checkEnv("PersistStrat")){
            string env_persist = gtc->getEnv("PersistStrat");
            if (env_persist == "DirWB"){
                to_be_persisted = new DirWB();
            } else if (env_persist == "PerEpoch"){
                to_be_persisted = new PerEpoch(gtc);
            } else if (env_persist == "BufferedWB"){
                to_be_persisted = new BufferedWB(gtc);
            } else {
                errexit("unrecognized 'persist' environment");
            }
        } else {
            // gtc->setEnv("PersistStrat", "PerEpoch");
            // to_be_persisted = new PerEpoch(gtc);
            gtc->setEnv("PersistSTrat", "BufferedWB");
            to_be_persisted = new BufferedWB(gtc);
        }

        if (gtc->checkEnv("Free")){
            string env_free = gtc->getEnv("Free");
            if (env_free == "PerEpoch"){
                to_be_freed = new PerEpochFreedContainer(this, gtc);
            } else if(env_free == "PerThread"){
                to_be_freed = new PerThreadFreedContainer(this, gtc);
            }else if (env_free == "No"){
                to_be_freed = new NoToBeFreedContainer(this);
            } else {
                errexit("unrecognized 'free' environment");
            }
        } else {
            to_be_freed = new PerEpochFreedContainer(this, gtc);
        }

        if (gtc->checkEnv("TransTracker")){
            string env_transcounter = gtc->getEnv("TransTracker");
            if (env_transcounter == "AtomicCounter"){
                trans_tracker = new AtomicTransactionTracker(this->global_epoch);
            } else if (env_transcounter == "ActiveThread"){
                trans_tracker = new FenceBeginTransactionTracker(this->global_epoch, task_num);
            } else if (env_transcounter == "CurrEpoch"){
                trans_tracker = new PerEpochTransactionTracker(this->global_epoch, task_num);
            } else {
                errexit("unrecognized 'transaction counter' environment");
            }
        } else {
            trans_tracker = new PerEpochTransactionTracker(this->global_epoch, task_num);
        }

        epoch_advancer = new DedicatedEpochAdvancer(gtc, this);

        // if (gtc->checkEnv("EpochAdvance")){
        //     string env_epochadvance = gtc->getEnv("EpochAdvance");
        //     if (env_epochadvance == "Global"){
        //         epoch_advancer = new GlobalCounterEpochAdvancer();
        //     } else if (env_epochadvance == "SingleThread"){
        //         epoch_advancer = new SingleThreadEpochAdvancer(gtc);
        //     } else if (env_epochadvance == "Dedicated"){
        //         epoch_advancer = new DedicatedEpochAdvancer(gtc, this);
        //     } else {
        //         errexit("unrecognized 'epoch advance' argument");
        //     }
        // } else {
        //     gtc->setEnv("EpochAdvance", "Dedicated");
        //     epoch_advancer = new DedicatedEpochAdvancer(gtc, this);
        // }

        // if (gtc->checkEnv("EpochFreq")){
        //     int env_epoch_advance = stoi(gtc->getEnv("EpochFreq"));
        //     if (gtc->getEnv("EpochAdvance") != "Dedicated" && env_epoch_advance > 63){
        //         errexit("invalid EpochFreq power");
        //     }
        //     epoch_advancer->set_epoch_freq(env_epoch_advance);
        // }
    }

    bool EpochSys::check_epoch(uint64_t c){
        return c == global_epoch->load(std::memory_order_seq_cst);
    }

    uint64_t EpochSys::begin_transaction(){
        uint64_t ret;
        do{
            ret = global_epoch->load(std::memory_order_seq_cst);
        } while(!trans_tracker->consistent_register_active(ret, ret));
        to_be_freed->free_on_new_epoch(ret);
        
        return ret;
    }

    void EpochSys::end_transaction(uint64_t c){
        trans_tracker->unregister_active(c);
        epoch_advancer->on_end_transaction(this, c);
    }

    void EpochSys::end_readonly_transaction(uint64_t c){
        trans_tracker->unregister_active(c);
    }

    // the same as end_readonly_transaction, but semantically different. Repeat to avoid confusion.
    void EpochSys::abort_transaction(uint64_t c){
        trans_tracker->unregister_active(c);
    }

    void EpochSys::validate_access(const PBlk* b, uint64_t c){
        if (c == NULL_EPOCH){
            errexit("access with NULL_EPOCH. BEGIN_OP not called?");
        }
        if (b->epoch > c){
            throw OldSeeNewException();
        }
    }

    // TODO (Hs): possible to move these into .hpp for inlining?
    void EpochSys::register_update_pblk(PBlk* b, uint64_t c){
        // to_be_persisted[c%4].push(b);
        if (c == NULL_EPOCH){
            // update before BEGIN_OP, return. This register will be done by BEGIN_OP.
            return;
        }
        to_be_persisted->register_persist(b, _ral->malloc_size(b), c);
    }

    uint64_t EpochSys::get_epoch(){
        return global_epoch->load(std::memory_order_acquire);
    }

    void EpochSys::set_epoch(uint64_t c){
        return global_epoch->store(c, std::memory_order_seq_cst);
    }

    // // Arg is epoch we think we're ending
    // void EpochSys::advance_epoch(uint64_t c){
    //     // TODO: if we go with one bookkeeping thread, remove unecessary synchronizations.

    //     // Free all retired blocks from 2 epochs ago
    //     if (!trans_tracker->consistent_register_bookkeeping(c-2, c)){
    //         return;
    //     }

    //     to_be_freed->help_free(c-2);

    //     trans_tracker->unregister_bookkeeping(c-2);

    //     // Wait until any other threads freeing such blocks are done
    //     while(!trans_tracker->no_bookkeeping(c-2)){
    //         if (global_epoch->load(std::memory_order_acquire) != c){
    //             return;
    //         }
    //     }

    //     // Wait until all threads active one epoch ago are done
    //     if (!trans_tracker->consistent_register_bookkeeping(c-1, c)){
    //         return;
    //     }
    //     while(!trans_tracker->no_active(c-1)){
    //         if (global_epoch->load(std::memory_order_acquire) != c){
    //             return;
    //         }
    //     }

    //     // Persist all modified blocks from 1 epoch ago
    //     // while(to_be_persisted->persist_epoch(c-1));
    //     to_be_persisted->persist_epoch(c-1);

    //     trans_tracker->unregister_bookkeeping(c-1);

    //     // Wait until any other threads persisting such blocks are done
    //     while(!trans_tracker->no_bookkeeping(c-1)){
    //         if (global_epoch->load(std::memory_order_acquire) != c){
    //             return;
    //         }
    //     }
    //     // persist_func::sfence(); // given the length of current epoch, we may not need this.
    //     // Actually advance the epoch
    //     global_epoch->compare_exchange_strong(c, c+1, std::memory_order_seq_cst);
    //     // Failure is harmless
    // }

    // this epoch advancing logic has been put into epoch advancers.
    // void EpochSys::advance_epoch_dedicated(){
    //     uint64_t c = global_epoch->load(std::memory_order_relaxed);
    //     // Free all retired blocks from 2 epochs ago
    //     to_be_freed->help_free(c-2);
    //     // Wait until all threads active one epoch ago are done
    //     while(!trans_tracker->no_active(c-1)){}
    //     // Persist all modified blocks from 1 epoch ago
    //     to_be_persisted->persist_epoch(c-1);
    //     persist_func::sfence();
    //     // Actually advance the epoch
    //     // global_epoch->compare_exchange_strong(c, c+1, std::memory_order_seq_cst);
    //     global_epoch->store(c+1, std::memory_order_seq_cst);
    // }

<<<<<<< HEAD
    // atomically set the current global epoch number
    void EpochSys::set_epoch(uint64_t c){
        global_epoch->store(c, std::memory_order_seq_cst);
    }

=======
>>>>>>> 01f74c3e657e6121b380288cb0b6ba398244f956
    void EpochSys::on_epoch_begin(uint64_t c){
        // does reclamation for c-2
        to_be_freed->help_free(c-2);
    }

    void EpochSys::on_epoch_end(uint64_t c){
        // Wait until all threads active one epoch ago are done
        while(!trans_tracker->no_active(c-1)){}
        // Persist all modified blocks from 1 epoch ago
        to_be_persisted->persist_epoch(c-1);
        persist_func::sfence();
    }

    std::unordered_map<uint64_t, PBlk*>* EpochSys::recover(const int rec_thd){
        std::unordered_map<uint64_t, PBlk*>* in_use = new std::unordered_map<uint64_t, PBlk*>();
#ifndef MNEMOSYNE
        bool clean_start;

        auto itr_raw = _ral->recover(rec_thd);

        sys_mode=RECOVER;
        // set system mode to RECOVER -- all PDELETE_DATA and PDELETE becomes no-ops.

        // make a whole pass thorugh all blocks, find the epoch block.
        epoch_container = nullptr;
        if(itr_raw[0].is_dirty()) {
            clean_start = false;
            std::cout<<"dirty restart"<<std::endl;
            // dirty restart, epoch system and app need to handle
        } else {
            std::cout<<"clean restart"<<std::endl;
            clean_start = true;
            // clean restart, epoch system and app may still need iter to do something
        }

        #pragma omp parallel num_threads(rec_thd)
        {
            int tid = omp_get_thread_num();
            for(; !itr_raw[tid].is_last(); ++itr_raw[tid]) { // iter++ is temporarily not supported
                PBlk* curr_blk = (PBlk*)*itr_raw[tid];
                // use curr_blk to do higher level recovery
                if (curr_blk->blktype == EPOCH){
                    epoch_container = (Epoch*) curr_blk;
                    global_epoch = &epoch_container->global_epoch;
                    // we continue this pass to the end to help ralloc recover.
                }
            }
        }

        std::cout<<"finished first traversal"<<std::endl;
        if (!epoch_container){
            errexit("epoch container not found during recovery.");
        }
        std::cout<<"epoch before crash:" << global_epoch->load() <<std::endl;

        // make a second pass through all blocks, compute a set of in-use blocks and return the others (to ralloc).
        uint64_t epoch_cap = global_epoch->load(std::memory_order_relaxed) - 2;
        std::unordered_set<PBlk*> not_in_use;
        std::unordered_set<uint64_t> delete_nodes;
        std::unordered_multimap<uint64_t, PBlk*> owned;
        std::mutex not_in_use_m;
        std::mutex delete_nodes_m;
        std::mutex in_use_m;
        std::mutex owned_m;

        itr_raw = _ral->recover(rec_thd);

        // Clear the heap
        if (epoch_cap < 1) {
            #pragma omp parallel num_threads(rec_thd)
            {
                int tid = omp_get_thread_num();
                for (; !itr_raw[tid].is_last(); ++itr_raw[tid]) {
                    _ral->deallocate(*itr_raw[tid],0);
                }
            }
            return in_use;
        }
        auto begin = chrono::high_resolution_clock::now();

        #pragma omp parallel num_threads(rec_thd)
        {
            int tid = omp_get_thread_num();
            std::unordered_set<PBlk*> _not_in_use;
            std::unordered_set<uint64_t> _delete_nodes;
            std::unordered_map<uint64_t, PBlk*> _in_use;
            std::unordered_multimap<uint64_t, PBlk*> _owned;

            for(; !itr_raw[tid].is_last(); ++itr_raw[tid]) { // iter++ is temporarily not supported
                PBlk* curr_blk = (PBlk*)*itr_raw[tid];
                // use curr_blk to do higher level recovery
                if (curr_blk->epoch == NULL_EPOCH || curr_blk->epoch > epoch_cap){
                    _not_in_use.insert(curr_blk);
                } else {
                    curr_blk->epoch = INIT_EPOCH + 2;
                    switch(curr_blk->blktype){
                        case OWNED:
                            _owned.insert(std::pair<uint64_t, PBlk*>(curr_blk->owner_id, curr_blk));
                            break;
                        case ALLOC:{
                                       auto insert_res = _in_use.insert({curr_blk->id, curr_blk});
                                       if (insert_res.second == false){
                                           if (clean_start){
                                               errexit("more than one record with the same id after a clean exit.");
                                           }
                                           _not_in_use.insert(curr_blk);
                                       }
                                   }
                                   break;
                        case UPDATE:{
                                        auto search = _in_use.find(curr_blk->id);
                                        if (search != _in_use.end()){
                                            if (clean_start){
                                                errexit("more than one record with the same id after a clean exit.");
                                            }
                                            if (curr_blk->epoch > search->second->epoch){
                                                _not_in_use.insert(search->second);
                                                search->second = curr_blk; // TODO: double-check if this is right.
                                            } else {
                                                _not_in_use.insert(curr_blk);
                                            }
                                        } else {
                                            _in_use.insert({curr_blk->id, curr_blk});
                                        }
                                    }
                                    break;
                        case DELETE:
                                    if (clean_start){
                                        errexit("delete node appears after a clean exit.");
                                    }
                                    _delete_nodes.insert(curr_blk->id);
                                    _not_in_use.insert(curr_blk);
                                    break;
                        case EPOCH:
                                    break;
                        default:
                                    errexit("wrong type of pblk discovered");
                                    break;
                    }
                }
            }

            #pragma omp critical
            delete_nodes.merge(_delete_nodes);
            #pragma omp critical    
            owned.merge(_owned);
            #pragma omp critical
            in_use->merge(_in_use);
            #pragma omp critical
            not_in_use.merge(_not_in_use);
            // If recovery ever fails, uncomment the below
            /*
            #pragma omp critical
            {
                for (auto tpl : _in_use) {
                    auto curr_blk = tpl.second;
                    if (curr_blk->blktype == UPDATE) {
                        auto ret = in_use->find(curr_blk->id);
                        if (ret != in_use->end()) {
                            if (curr_blk->epoch > ret->second->epoch) {
                                not_in_use.insert(ret->second);
                                ret->second = curr_blk;
                            } else {
                                not_in_use.insert(curr_blk);
                            }
                        } else {
                            in_use->insert({curr_blk->id, curr_blk});
                        }
                    } else if (curr_blk->blktype == ALLOC) { 
                        auto ret = in_use->insert({curr_blk->id, curr_blk});
                        if (ret.second == false) {
                            not_in_use.insert(curr_blk);
                        }
                    } else {
                        in_use->insert({curr_blk->id, curr_blk});
                    }
                }
                not_in_use.merge(_not_in_use);
            }*/
        }
        
        auto end = chrono::high_resolution_clock::now();
        auto dur = end - begin;
        auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Second pass completed in " << dur_ms << "ms" << std::endl;
    
        std::cout << "deleted(" << delete_nodes.size() << "), not_in_use(" << not_in_use.size() << "), in_use(" << in_use->size() << "), owned(" << owned.size() << ")" << std::endl;
        if (clean_start){
            return in_use;
        }

        
        begin = chrono::high_resolution_clock::now();
        // make a pass through in-use blocks and owned blocks, remove those marked by delete_nodes:
        for (auto itr = delete_nodes.begin(); itr != delete_nodes.end(); itr++){
            // remove deleted in-use blocks

            auto deleted = in_use->extract(*itr);
            if (!deleted.empty()){
                not_in_use.insert(deleted.mapped());
            }
            // remove deleted owned blocks
            auto owned_blks = owned.equal_range(*itr);
            for (auto owned_itr = owned_blks.first; owned_itr != owned_blks.second; owned_itr++){
                not_in_use.insert(owned_itr->second);
            }
            owned.erase(*itr);
        }

        end = chrono::high_resolution_clock::now();
        dur = end - begin;
        dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Third pass completed in " << dur_ms << "ms" << std::endl;

        begin = chrono::high_resolution_clock::now();
        // make a pass through owned blocks, remove orphaned blocks:
        std::unordered_set<uint64_t> orphaned;
        for (auto itr = owned.begin(); itr != owned.end(); itr++){
            if (in_use->find(itr->first) == in_use->end()){
                orphaned.insert(itr->first);
            }
        }
        for (auto itr = orphaned.begin(); itr != orphaned.end(); itr++){
            auto orphaned_blks = owned.equal_range(*itr);
            for (auto orphaned_itr = orphaned_blks.first; orphaned_itr != orphaned_blks.second; orphaned_itr++){
                not_in_use.insert(orphaned_itr->second);
            }
            owned.erase(*itr); // we don't actually need to do this.
        }
        end = chrono::high_resolution_clock::now();
        dur = end - begin;
        dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Fourth pass completed in " << dur_ms << "ms" << std::endl;

        // reclaim all nodes in not_in_use bag
        for (auto itr = not_in_use.begin(); itr != not_in_use.end(); itr++){
            // we can't call delete here: the PBlk may have null vtable pointer
            _ral->deallocate(*itr);
        }

        // set system mode back to online
        sys_mode = ONLINE;
        reset();

        std::cout<<"returning from EpochSys Recovery."<<std::endl;
#endif /* !MNEMOSYNE */
        return in_use;
    }
}

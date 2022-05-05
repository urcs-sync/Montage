#include "EpochSys.hpp"
#include "Recoverable.hpp"

#include <omp.h>
#include <atomic>

namespace pds{

    void sc_desc_t::try_complete(Recoverable* ds, uint64_t addr){
        lin_var _d = var.load();
        // int ret = 0;
        if(_d.val!=addr) return;
        if(in_progress(_d)){
            if(ds->check_epoch(epoch)){
                // ret = 2;
                // ret |= commit(_d);
                commit(_d);
            } else {
                // ret = 4;
                // ret |= abort(_d);
                abort(_d);
            }
        }
        cleanup(_d);
    }

    void sc_desc_t::try_abort(uint64_t expected_e){
        lin_var _d = var.load();
        if(epoch == expected_e && in_progress(_d)){
            abort(_d);
        }
        // epoch advancer aborts but doesn't clean up descriptors
    }

    thread_local int EpochSys::tid = -1;
    std::atomic<int> EpochSys::esys_num(0);
    void EpochSys::parse_env(){
        if (epoch_advancer){
            delete epoch_advancer;
        }
        if (trans_tracker){
            delete trans_tracker;
        }
        if (to_be_persisted) {
            delete to_be_persisted;
        }
        if (to_be_freed) {
            delete to_be_freed;
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
                persisted_epochs = new IncreasingMindicator(task_num);
                return;
            }
        }

        if (gtc->checkEnv("PersistStrat")){
            string env_persist = gtc->getEnv("PersistStrat");
            if (env_persist == "DirWB"){
                to_be_persisted = new DirWB(_ral, gtc->task_num);
            } else if (env_persist == "BufferedWB"){
                to_be_persisted = new BufferedWB(gtc, _ral);
            } else {
                errexit("unrecognized 'persist' environment");
            }
        } else {
            gtc->setEnv("PersistStrat", "BufferedWB");
            to_be_persisted = new BufferedWB(gtc, _ral);
        }

        if (gtc->checkEnv("Free")){
            string env_free = gtc->getEnv("Free");
            if (env_free == "PerEpoch"){
                to_be_freed = new PerEpochFreedContainer(this, gtc);
            } else if(env_free == "ThreadLocal"){
                to_be_freed = new ThreadLocalFreedContainer(this, gtc);
            }else if (env_free == "No"){
                to_be_freed = new NoToBeFreedContainer(this);
            } else {
                errexit("unrecognized 'free' environment");
            }
        } else {
            to_be_freed = new ThreadLocalFreedContainer(this, gtc);
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

        if (gtc->checkEnv("PersistTracker")){
            string env_persisttracker = gtc->getEnv("PersistTracker");
            if (env_persisttracker == "IncreasingMindicator"){
                persisted_epochs = new IncreasingMindicator(task_num);
            } else if (env_persisttracker == "Mindicator"){
                persisted_epochs = new Mindicator(task_num);
            } else {
                errexit("unrecognized 'persist tracker' environment");
            }
        } else {
            persisted_epochs = new IncreasingMindicator(task_num);
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
        local_descs[tid]->reinit();
        do{
            ret = global_epoch->load(std::memory_order_seq_cst);
        } while(!trans_tracker->consistent_register_active(ret, ret));
        auto last_epoch = last_epochs[tid].ui;
        if(last_epoch != ret){
            last_epochs[tid].ui = ret;
            if (last_epoch == ret - 1) {
                // we just entered a new epoch.
                persisted_epochs->first_write_on_new_epoch(ret, EpochSys::tid);
            }
            // persist past epochs if a target needs us.
            uint64_t persist_until =
                min(epoch_advancer->ongoing_target() - 2, ret - 1);
            while (true) {
                uint64_t to_persist =
                    persisted_epochs->next_epoch_to_persist(EpochSys::tid);
                if (to_persist == NULL_EPOCH || to_persist > persist_until) {
                    break;
                }
                to_be_persisted->persist_epoch_local(to_persist, EpochSys::tid);
                persisted_epochs->after_persist_epoch(to_persist,
                                                      EpochSys::tid);
            }
        }
        
        to_be_freed->free_on_new_epoch(ret);
        local_descs[tid]->set_up_epoch(ret);
        // Wentao: in blocking EpochSys, there's no need to register
        // desc in TBP or persist it at all, because unlike
        // nonblocking version, transient desc is enough here and we
        // don't need to rely on persistent OPID to determine whether
        // a payload corresponds to a committed or aborted op. During
        // recovery, just throwing away everything from the last
        // epochs works.
        return ret;
    }

    void EpochSys::end_transaction(uint64_t c){
        last_epochs[tid].ui = c;
        trans_tracker->unregister_active(c);
        epoch_advancer->on_end_transaction(this, c);
    }

    uint64_t EpochSys::begin_reclaim_transaction(){
        uint64_t ret;
        do{
            ret = global_epoch->load(std::memory_order_seq_cst);
        } while(!trans_tracker->consistent_register_active(ret, ret));
        to_be_freed->free_on_new_epoch(ret);
        return ret;
    }

    void EpochSys::end_reclaim_transaction(uint64_t c){
        last_epochs[tid].ui = c;
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

    void EpochSys::register_alloc_pblk(PBlk* b, uint64_t c){
        // static_assert(std::is_convertible<T*, PBlk*>::value,
        //     "T must inherit PBlk as public");
        // static_assert(std::is_copy_constructible<T>::value,
        //             "requires copying");
        PBlk* blk = b;
        assert(c != NULL_EPOCH);
        blk->epoch = c;
        assert(blk->blktype == INIT || blk->blktype == OWNED); 
        if (blk->blktype == INIT){
            blk->blktype = ALLOC;
        }
        if (blk->id == 0){
            blk->id = uid_generator.get_id(tid);
        }

        to_be_persisted->register_persist(blk, c);
        PBlk* data = blk->get_data();
        if (data){
            register_alloc_pblk(data, c);
        }
    }

    // TODO (Hs): possible to move these into .hpp for inlining?
    void EpochSys::register_update_pblk(PBlk* b, uint64_t c){
        // to_be_persisted[c%4].push(b);
        if (c == NULL_EPOCH){
            // update before BEGIN_OP, return. This register will be done by BEGIN_OP.
            return;
        }
        to_be_persisted->register_persist(b, c);
    }

    void EpochSys::prepare_retire_pblk(PBlk* b, const uint64_t& c, std::vector<std::pair<PBlk*,PBlk*>>& pending_retires){
        pending_retires.emplace_back(b, nullptr);
    }

    void EpochSys::prepare_retire_pblk(std::pair<PBlk*,PBlk*>& pending_retire, const uint64_t& c){ 
         // noop
     }

    void EpochSys::withdraw_retire_pblk(PBlk* b, uint64_t c){
         // noop
     }

    void EpochSys::retire_pblk(PBlk* b, uint64_t c, PBlk* anti){
        PBlk* blk = b;
        if (blk->retire != nullptr){
            errexit("double retire error, or this block was tentatively retired before recent crash.");
        }
        uint64_t e = blk->epoch;
        PBlkType blktype = blk->blktype;
        if (e > c){
            throw OldSeeNewException();
        } else if (e == c){
            // retiring a block updated/allocated in the same epoch.
            // changing it directly to a DELETE node without putting it in to_be_freed list.
            if (blktype == ALLOC || blktype == UPDATE){
                blk->blktype = DELETE;
            } else {
                errexit("wrong type of PBlk to retire.");
            }
        } else {
            // note this actually modifies 'retire' field of a PBlk from the past
            // Which is OK since nobody else will look at this field.
            blk->retire = new_pblk<PBlk>(*b);
            blk->retire->blktype = DELETE;
            blk->retire->epoch = c;
            to_be_persisted->register_persist(blk->retire, c);
        }
        to_be_persisted->register_persist(b, c);
    }

    uint64_t EpochSys::get_epoch(){
        return global_epoch->load(std::memory_order_acquire);
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

    // atomically set the current global epoch number
    void EpochSys::set_epoch(uint64_t c){
        global_epoch->store(c, std::memory_order_seq_cst);
    }

    void EpochSys::on_epoch_begin(uint64_t c){
        // does reclamation for c-2
        to_be_freed->help_free(c-2);
    }

    void EpochSys::on_epoch_end(uint64_t c){
        // Wait until all threads active one epoch ago are done
        // TODO: optimization: persist inactive threads first.
        while(!trans_tracker->no_active(c-1)){}

        // take modular, in case of dedicated epoch advancer calling this function.
        int curr_thread = EpochSys::tid % gtc->task_num;
        curr_thread = persisted_epochs->next_thread_to_persist(c-1, curr_thread);
        // check the top of mindicator to get the last persisted epoch globally
        while(curr_thread >= 0){
            // traverse mindicator to persist each leaf lagging behind, until the top meets requirement
            to_be_persisted->persist_epoch_local(c-1, curr_thread);
            persisted_epochs->after_persist_epoch(c-1, curr_thread);
            curr_thread = persisted_epochs->next_thread_to_persist(c-1, curr_thread);
        }
    }

    std::unordered_map<uint64_t, PBlk*>* EpochSys::recover(const int rec_thd){
        std::unordered_map<uint64_t, PBlk*>* in_use = new std::unordered_map<uint64_t, PBlk*>();
        uint64_t max_epoch = 0;
#ifndef MNEMOSYNE
        bool clean_start;
        auto itr_raw = _ral->recover(rec_thd);
        sys_mode=RECOVER;
        // set system mode to RECOVER -- all PDELETE_DATA and PDELETE becomes no-ops.
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

        std::atomic<int> curr_reporting;
        curr_reporting = 0;
        pthread_barrier_t sync_point;
        pthread_barrier_init(&sync_point, NULL, rec_thd);
        std::vector<std::thread> workers;
        
        std::unordered_set<uint64_t> deleted_ids;
        auto begin = chrono::high_resolution_clock::now();
        auto end = begin;
        for (int rec_tid = 0; rec_tid < rec_thd; rec_tid++) {
            workers.emplace_back(std::thread([&, rec_tid]() {
                hwloc_set_cpubind(gtc->topology, gtc->affinities[rec_tid]->cpuset, HWLOC_CPUBIND_THREAD);
                thread_local uint64_t max_epoch_local = 0;
                thread_local std::unordered_multimap<uint64_t, PBlk*> anti_nodes_local;
                thread_local std::unordered_set<uint64_t> deleted_ids_local;
                // make the first whole pass thorugh all blocks, find the epoch block
                // and help Ralloc fully recover by completing the pass.
                for (; !itr_raw[rec_tid].is_last(); ++itr_raw[rec_tid]){
                    PBlk* curr_blk = (PBlk*) *itr_raw[rec_tid];
                    if (curr_blk->blktype == EPOCH){
                        epoch_container = (Epoch*) curr_blk;
                        global_epoch = &epoch_container->global_epoch;
                        max_epoch_local = std::max(global_epoch->load(), max_epoch_local);
                    } else if (curr_blk->blktype == DELETE){
                        if (clean_start) {
                            errexit("delete node appears after a clean exit.");
                        }
                        anti_nodes_local.insert({curr_blk->get_epoch(), curr_blk});
                        if (curr_blk->get_epoch() != NULL_EPOCH) {
                            deleted_ids_local.insert(curr_blk->get_id());
                        }
                    }
                    max_epoch_local = std::max(max_epoch_local, curr_blk->get_epoch());
                }
                // report after the first pass:
                // calculate the maximum epoch number as the current epoch.
                pthread_barrier_wait(&sync_point);
                if (!epoch_container){
                    errexit("epoch container not found during recovery");
                }
                while(curr_reporting.load() != rec_tid);
                if (rec_tid == 0) {
                    end = chrono::high_resolution_clock::now();
                    auto dur = end - begin;
                    std::cout << "Spent "
                              << std::chrono::duration_cast<
                                     std::chrono::milliseconds>(dur)
                                     .count()
                              << "ms in first pass" << std::endl;
                    begin = chrono::high_resolution_clock::now();
                }
                max_epoch = std::max(max_epoch, max_epoch_local);
                if (rec_tid == rec_thd-1){
                    end = chrono::high_resolution_clock::now();
                    auto dur = end - begin;
                    std::cout << "Spent " << std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()
                              << "ms in first merge"
                              << std::endl;
                    begin = chrono::high_resolution_clock::now();
                }
                curr_reporting.store((rec_tid+1) % rec_thd);
                
                pthread_barrier_wait(&sync_point);
                // remove premature deleted_ids, and merge deleted_ids.
                for (uint64_t e : std::vector<uint64_t>{max_epoch, max_epoch-1}){
                    auto immature = anti_nodes_local.equal_range(e);
                    for (auto itr = immature.first; itr != immature.second; itr++){
                        deleted_ids_local.erase(itr->second->get_id());
                    }
                }
                // merge the results of deleted_ids_local
                pthread_barrier_wait(&sync_point);
                while (curr_reporting.load() != rec_tid);
                deleted_ids.merge(deleted_ids_local);
                curr_reporting.store((rec_tid + 1) % rec_thd);

                // make a second pass through all pblks
                pthread_barrier_wait(&sync_point);
                if (rec_tid == 0){
                    itr_raw = _ral->recover(rec_thd);
                }
                pthread_barrier_wait(&sync_point);
                uint64_t epoch_cap = max_epoch - 2;
                thread_local std::vector<PBlk*> not_in_use_local;
                thread_local std::unordered_map<uint64_t, PBlk*> in_use_local;
                thread_local int second_pass_blks = 0;
                for (; !itr_raw[rec_tid].is_last(); ++itr_raw[rec_tid]) {
                    second_pass_blks++;
                    PBlk* curr_blk = (PBlk*)*itr_raw[rec_tid];
                    // put all premature pblks and those marked by
                    // deleted_ids in not_in_use
                    if (// leave DESC blocks untouched for now.
                        curr_blk->blktype != DESC &&
                        // DELETE blocks are already put into anti_nodes_local.
                        curr_blk->blktype != DELETE && (
                            // block without epoch number, probably just inited
                            curr_blk->epoch == NULL_EPOCH || 
                            // premature pblk
                            curr_blk->epoch > epoch_cap || 
                            // marked deleted by some anti-block
                            deleted_ids.find(curr_blk->get_id()) != deleted_ids.end() 
                        )) {
                        not_in_use_local.push_back(curr_blk);
                    } else {
                        // put all others in in_use while resolve conflict
                        switch (curr_blk->blktype) {
                            case OWNED:
                                errexit(
                                    "OWNED isn't a valid blktype in this "
                                    "version.");
                                break;
                            case ALLOC: {
                                auto insert_res =
                                    in_use_local.insert({curr_blk->id, curr_blk});
                                if (insert_res.second == false) {
                                    if (clean_start) {
                                        errexit(
                                            "more than one record with the "
                                            "same id after a clean exit.");
                                    }
                                    not_in_use_local.push_back(curr_blk);
                                }
                            } break;
                            case UPDATE: {
                                auto search = in_use_local.find(curr_blk->id);
                                if (search != in_use_local.end()) {
                                    if (clean_start) {
                                        errexit(
                                            "more than one record with the "
                                            "same id after a clean exit.");
                                    }
                                    if (curr_blk->epoch >
                                        search->second->epoch) {
                                        not_in_use_local.push_back(search->second);
                                        search->second =
                                            curr_blk;  // TODO: double-check if
                                                       // this is right.
                                    } else {
                                        not_in_use_local.push_back(curr_blk);
                                    }
                                } else {
                                    in_use_local.insert({curr_blk->id, curr_blk});
                                }
                            } break;
                            case DELETE:
                            case EPOCH:
                            case DESC: // TODO: allocate DESC in DRAM instead of NVM
                                break;
                            default:
                                errexit("wrong type of pblk discovered");
                                break;
                        }
                    }
                }
                // merge the results of in_use, resolve conflict
                pthread_barrier_wait(&sync_point);
                while (curr_reporting.load() != rec_tid);
                if (rec_tid == 0) {
                    end = chrono::high_resolution_clock::now();
                    auto dur = end - begin;
                    std::cout << "Spent "
                              << std::chrono::duration_cast<
                                     std::chrono::milliseconds>(dur)
                                     .count()
                              << "ms in second pass" << std::endl;
                    begin = chrono::high_resolution_clock::now();
                }
                std::cout<<"second pass blk count:"<<second_pass_blks<<std::endl;
                for (auto itr : in_use_local) {
                    auto found = in_use->find(itr.first);
                    if (found == in_use->end()) {
                        in_use->insert({itr.first, itr.second});
                    } else if (found->second->get_epoch() <
                               itr.second->get_epoch()) {
                        not_in_use_local.push_back(found->second);
                        found->second = itr.second;
                    } else {
                        not_in_use_local.push_back(itr.second);
                    }
                }
                if (rec_tid == rec_thd - 1) {
                    end = chrono::high_resolution_clock::now();
                    auto dur = end - begin;
                    std::cout << "Spent "
                              << std::chrono::duration_cast<
                                     std::chrono::milliseconds>(dur)
                                     .count()
                              << "ms in second merge" << std::endl;
                    begin = chrono::high_resolution_clock::now();
                }
                curr_reporting.store((rec_tid + 1) % rec_thd);
                // clean up not_in_use and anti-nodes
                for (auto itr : not_in_use_local) {
                    itr->set_epoch(NULL_EPOCH);
                    _ral->deallocate(itr, rec_tid);
                }
                for (auto itr : anti_nodes_local) {
                    itr.second->set_epoch(NULL_EPOCH);
                    _ral->deallocate(itr.second, rec_tid);
                }
                pthread_barrier_wait(&sync_point);
                if (rec_tid == rec_thd - 1) {
                    end = chrono::high_resolution_clock::now();
                    auto dur = end - begin;
                    std::cout << "Spent "
                              << std::chrono::duration_cast<
                                     std::chrono::milliseconds>(dur)
                                     .count()
                              << "ms in deallocation" << std::endl;
                    begin = chrono::high_resolution_clock::now();
                }
            })); // workers.emplace_back()
        } // for (rec_thd)
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        // set system mode back to online
        sys_mode = ONLINE;
        reset();

        std::cout<<"returning from EpochSys Recovery."<<std::endl;
#endif /* !MNEMOSYNE */
        return in_use;
    }

    void nbEpochSys::reset(){
        EpochSys::reset();
        // TODO: only nbEpochSys needs persistent descs. consider move all
        // inits into nbEpochSys.
        for (int i = 0; i < gtc->task_num; i++) {
            to_be_persisted->init_desc_local(local_descs[i], i);
        }
    }

    void nbEpochSys::parse_env(){
        if (epoch_advancer){
            delete epoch_advancer;
        }
        if (trans_tracker){
            delete trans_tracker;
        }
        if (to_be_persisted) {
            delete to_be_persisted;
        }
        if (to_be_freed) {
            delete to_be_freed;
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
                persisted_epochs = new IncreasingMindicator(task_num);
                return;
            }
        }

        if (gtc->checkEnv("PersistStrat")){
            string env_persist = gtc->getEnv("PersistStrat");
            if (env_persist == "DirWB"){
                to_be_persisted = new DirWB(_ral, gtc->task_num);
            } else if (env_persist == "PerEpoch"){
                errexit("nbEpochSys isn't compatible with PerEpoch!");
            } else if (env_persist == "BufferedWB"){
                to_be_persisted = new BufferedWB(gtc, _ral);
            } else {
                errexit("unrecognized 'persist' environment");
            }
        } else {
            gtc->setEnv("PersistStrat", "BufferedWB");
            to_be_persisted = new BufferedWB(gtc, _ral);
        }

        if (gtc->checkEnv("Free")){
            string env_free = gtc->getEnv("Free");
            if (env_free == "PerEpoch"){
                to_be_freed = new PerEpochFreedContainer(this, gtc);
            } else if (env_free == "No"){
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

        if (gtc->checkEnv("PersistTracker")){
            string env_persisttracker = gtc->getEnv("PersistTracker");
            if (env_persisttracker == "IncreasingMindicator"){
                persisted_epochs = new IncreasingMindicator(task_num);
            } else if (env_persisttracker == "Mindicator"){
                persisted_epochs = new Mindicator(task_num);
            } else {
                errexit("unrecognized 'persist tracker' environment");
            }
        } else {
            persisted_epochs = new IncreasingMindicator(task_num);
        }

        epoch_advancer = new DedicatedEpochAdvancerNbSync(gtc, this);

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

    void nbEpochSys::register_alloc_pblk(PBlk* b, uint64_t c){
        // static_assert(std::is_convertible<T*, PBlk*>::value,
        //     "T must inherit PBlk as public");
        // static_assert(std::is_copy_constructible<T>::value,
        //             "requires copying");
        b->set_tid_sn(tid,get_dcss_desc()->get_sn());
        PBlk* blk = b;
        assert(c != NULL_EPOCH);
        blk->epoch = c;
        assert(blk->blktype == INIT || blk->blktype == OWNED); 
        if (blk->blktype == INIT){
            blk->blktype = ALLOC;
        }
        if (blk->id == 0){
            blk->id = uid_generator.get_id(tid);
        }

        to_be_persisted->register_persist(blk, c);
        PBlk* data = blk->get_data();
        if (data){
            register_alloc_pblk(data, c);
        }
    }

    void nbEpochSys::prepare_retire_pblk(PBlk* b, const uint64_t& c, std::vector<std::pair<PBlk*,PBlk*>>& pending_retires){
        PBlk* blk = b;
        uint64_t e = blk->epoch;
        PBlkType blktype = blk->blktype;
        if (e > c){
            throw OldSeeNewException();
        } else {
            PBlk* anti = new_pblk<PBlk>(*b);
            anti->blktype = DELETE;
            anti->epoch = c;
            anti->set_tid_sn(tid, get_dcss_desc()->get_sn());
            pending_retires.emplace_back(blk, anti);
            // it may be registered in a newer bucket, but it's safe.
            to_be_persisted->register_persist(anti, c);
        }
    }

     void nbEpochSys::prepare_retire_pblk(std::pair<PBlk*,PBlk*>& pending_retire, const uint64_t& c){
        assert(pending_retire.second==nullptr);

        PBlk* blk = pending_retire.first;
        uint64_t e = blk->epoch;
        PBlkType blktype = blk->blktype;
        if (e > c){
            throw OldSeeNewException();
        } else {
            PBlk* anti = new_pblk<PBlk>(*blk);
            anti->blktype = DELETE;
            anti->epoch = c;
            anti->set_tid_sn(tid, get_dcss_desc()->get_sn());
            pending_retire.second = anti;
            // it may be registered in a newer bucket, but it's safe.
            to_be_persisted->register_persist(anti, c);
        }
    }

    void nbEpochSys::withdraw_retire_pblk(PBlk* b, uint64_t c){
        // Wentao: WARNING!! Here we directly deallocate
        // anti-payloads; those deallocated may still be in
        // to_be_persisted, but it's safe to flush after
        // deallocation if the address is still valid, which is
        // true in Ralloc. If in the future Ralloc changes or
        // different allocator is used, be careful about this!
        if(b!=nullptr){
            delete_pblk(b, c);
        }
     }

    void nbEpochSys::retire_pblk(PBlk* b, uint64_t c, PBlk* anti){
        PBlk* blk = b;
        if (blk->retire != nullptr){
            errexit("double retire error, or this block was tentatively retired before recent crash.");
        }
        uint64_t e = blk->epoch;
        PBlkType blktype = blk->blktype;
        assert (e <= c);
        // note this actually modifies 'retire' field of a PBlk from the past
        // Which is OK since nobody else will look at this field.
        assert(anti!=nullptr);
        // 'retire' field isn't persistent, so no flush is needed
        blk->retire = anti;
    }

    void nbEpochSys::local_free(uint64_t c){
        auto last_epoch = last_epochs[tid].ui;
        if(last_epoch==c) return;
        // There are at most three buckets not cleaned up,
        // last_epoch-1, last_epoch, and last_epoch+1. Also, when
        // begin in epoch c, only buckets <= c-2 should be cleaned
        for(uint64_t i = last_epoch-1; 
            i <= min(last_epoch+1,c-2);
            i++){
            to_be_freed->help_free_local(i);
            persist_func::sfence();
        }
    }

    void nbEpochSys::local_persist(uint64_t c){
        auto last_epoch = last_epochs[tid].ui;
        if(last_epoch == c) return;
        persisted_epochs->first_write_on_new_epoch(c,EpochSys::tid);
        /* Strategy 1: always flush TBP[last_epoch] */
        // There are at most one bucket not cleaned up, last_epoch.
        // Also, when begin in epoch c, only buckets <= c-2 should be
        // cleaned
        to_be_persisted->persist_epoch_local(last_epoch, EpochSys::tid);
        uint64_t to_persist =
            persisted_epochs->next_epoch_to_persist(EpochSys::tid);
        if(to_persist<=last_epoch){
            persisted_epochs->after_persist_epoch(last_epoch, EpochSys::tid);
        }
        persist_func::sfence();
#if 0
        // Wentao: This strategy is incorrect!!! 
        // Consider the case where T1 aborts in c-1 and places reset
        // items to TBP[c-1], but after a concurrent epoch advance
        // from c to c+1 goes through everyone's TBP[c-1]. And before
        // the advance updates the epoch to c+1, T1 initiates another
        // transaction in c and updates its descriptor. If T1 doesn't
        // write back TBP[c-1] before updating its descriptor, the
        // recovery from a crash in c+1 may think those payloads are
        // valid.

        /* Strategy 2: flush TBP[last_epoch] if it <= c-2 and then
            * check if there needs a help */
        if(last_epoch<=c-2){
            // clean up potential leftover from last txn due to abortion
            to_be_persisted->persist_epoch_local(last_epoch, EpochSys::tid);
            persist_func::sfence();
        } else {
            // persist past epochs if a target needs us.
            uint64_t persist_until =
                min(epoch_advancer->ongoing_target() - 2, c-1);
            while (true) {
                uint64_t to_persist =
                    persisted_epochs->next_epoch_to_persist(EpochSys::tid);
                if (to_persist == NULL_EPOCH || to_persist > persist_until) {
                    break;
                }
                to_be_persisted->persist_epoch_local(to_persist, EpochSys::tid);
                persisted_epochs->after_persist_epoch(to_persist, EpochSys::tid);
            }
        }
#endif /* 0 */
    }

    uint64_t nbEpochSys::begin_transaction(){
        uint64_t ret;
        ret = global_epoch->load(std::memory_order_seq_cst);
        local_persist(ret);
        local_descs[tid]->reinit();
        local_descs[tid]->set_up_epoch(ret);
        to_be_persisted->register_persist_desc_local(ret, EpochSys::tid);
        local_free(ret);
        return ret;
    }

    void nbEpochSys::end_transaction(uint64_t c){
        last_epochs[tid].ui = c;
        epoch_advancer->on_end_transaction(this, c);
    }

    uint64_t nbEpochSys::begin_reclaim_transaction(){
        uint64_t ret;
        ret = global_epoch->load(std::memory_order_seq_cst);
        local_persist(ret);
        local_free(ret);
        return ret;
    }

    void nbEpochSys::end_reclaim_transaction(uint64_t c){
        last_epochs[tid].ui = c;
        epoch_advancer->on_end_transaction(this, c);
    }
    void nbEpochSys::on_epoch_begin(uint64_t c){
        // do nothing -- memory reclamation is done thread-locally in nbEpochSys
    }

    void nbEpochSys::on_epoch_end(uint64_t c){
        // take modular, in case of dedicated epoch advancer calling this function.
        int curr_thread = EpochSys::tid % gtc->task_num;
        curr_thread = persisted_epochs->next_thread_to_persist(c-1, curr_thread);
        // check the top of mindicator to get the last persisted epoch globally
        while(curr_thread >= 0){
            // traverse mindicator to persist each leaf lagging behind, until the top meets requirement
            local_descs[curr_thread]->try_abort(c-1); // lazily abort ongoing transactions
            to_be_persisted->persist_epoch_local(c-1, curr_thread);
            persisted_epochs->after_persist_epoch(c-1, curr_thread);
            curr_thread = persisted_epochs->next_thread_to_persist(c-1, curr_thread);
        }
        // a lock-prefixed instruction (CAS) must have taken place inside Mindicator,
        // so no need to explicitly issue fence here.
        // persist_func::sfence();

        // old implementation:
        // try to abort all ongoing transactions
        // for(int i=0; i<task_num; i++){
        //     local_descs[i]->try_abort(c-1);
        // }
        // // Persist all modified blocks from 1 epoch ago
        // to_be_persisted->persist_epoch(c-1);
        // persist_func::sfence();
    }

    std::unordered_map<uint64_t, PBlk*>* nbEpochSys::recover(const int rec_thd) {
        std::unordered_map<uint64_t, PBlk*>* in_use = new std::unordered_map<uint64_t, PBlk*>();
        std::unordered_map<uint64_t, sc_desc_t*> descs;  //tid->desc
        uint64_t max_tid = 0;
        uint64_t max_epoch = 0;
#ifndef MNEMOSYNE
        bool clean_start;
        auto itr_raw = _ral->recover(rec_thd);
        sys_mode = RECOVER;
        // set system mode to RECOVER -- all PDELETE_DATA and PDELETE becomes no-ops.
        epoch_container = nullptr;
        if (itr_raw[0].is_dirty()) {
            clean_start = false;
            std::cout << "dirty restart" << std::endl;
            // dirty restart, epoch system and app need to handle
        } else {
            std::cout << "clean restart" << std::endl;
            clean_start = true;
            // clean restart, epoch system and app may still need iter to do something
        }

        std::atomic<int> curr_reporting;
        curr_reporting = 0;
        pthread_barrier_t sync_point;
        pthread_barrier_init(&sync_point, NULL, rec_thd);
        std::vector<std::thread> workers;

        std::unordered_set<uint64_t> deleted_ids;

        for (int rec_tid = 0; rec_tid < rec_thd; rec_tid++) {
            workers.emplace_back(std::thread([&, rec_tid]() {
                hwloc_set_cpubind(gtc->topology,
                                  gtc->affinities[rec_tid]->cpuset,
                                  HWLOC_CPUBIND_THREAD);
                thread_local uint64_t max_epoch_local = 0;
                thread_local uint64_t max_tid_local = -1;
                thread_local std::vector<PBlk*> anti_nodes_local;
                thread_local std::unordered_set<uint64_t> deleted_ids_local;
                thread_local std::unordered_map<uint64_t, sc_desc_t*> descs_local;
                // make the first whole pass thorugh all blocks, find the epoch block
                // and help Ralloc fully recover by completing the pass.
                for (; !itr_raw[rec_tid].is_last(); ++itr_raw[rec_tid]) {
                    PBlk* curr_blk = (PBlk*)*itr_raw[rec_tid];
                    if (curr_blk->blktype == EPOCH) {
                        epoch_container = (Epoch*)curr_blk;
                        global_epoch = &epoch_container->global_epoch;
                        max_epoch_local = std::max(global_epoch->load(), max_epoch_local);
                    } else if (curr_blk->blktype == DESC) {
                        // since sc_desc_t isn't a derived class of PBlk
                        // anymore, we have to reinterpret cast instead of
                        // dynamic cast
                        auto* tmp = reinterpret_cast<sc_desc_t*>(curr_blk);
                        assert(tmp != nullptr);
                        uint64_t curr_tid = tmp->get_tid();
                        max_tid_local = std::max(max_tid_local, curr_tid);
                        descs_local[curr_tid] = tmp;
                    } else if (curr_blk->blktype == DELETE) {
                        anti_nodes_local.push_back(curr_blk);
                        if (curr_blk->get_epoch() != NULL_EPOCH){
                            deleted_ids_local.insert(curr_blk->get_id());
                        }
                    }
                    max_epoch_local = std::max(max_epoch_local, curr_blk->get_epoch());
                }
                // report after the first pass:
                // calculate the maximum epoch number as the current epoch.
                // calculate the maximum tid
                // merge descs
                pthread_barrier_wait(&sync_point);
                if (!epoch_container) {
                    errexit("epoch container not found during recovery");
                }
                while (curr_reporting.load() != rec_tid)
                    ;
                max_epoch = std::max(max_epoch, max_epoch_local);
                max_tid = std::max(max_tid, max_tid_local);
                descs.merge(descs_local);
                if (rec_tid == rec_thd - 1) {
                    // some sanity check
                    // in data structures with background threads,
                    // these don't always hold
                    // assert(descs.size() == max_tid + 1);
                    // for (uint64_t i = 0; i < max_tid; i++) {
                    //     assert(descs.find(i) != descs.end());
                    // }
                }
                curr_reporting.store((rec_tid + 1) % rec_thd);

                uint64_t epoch_cap = max_epoch - 2;
                pthread_barrier_wait(&sync_point);
                // remove premature deleted_ids
                for (auto n : anti_nodes_local){
                    auto curr_tid = n->get_tid();
                    auto curr_sn = n->get_sn();
                    if ( // anti node belongs to an epoch too new
                        n->get_epoch() > epoch_cap ||
                        // transaction is not registered
                        curr_sn > descs[curr_tid]->get_sn() ||
                        // transaction registered but not committed
                        (curr_sn == descs[curr_tid]->get_sn() && !descs[curr_tid]->committed())){
                        deleted_ids_local.erase(n->get_id());
                    }
                }
                // merge the results of deleted_ids_local
                pthread_barrier_wait(&sync_point);
                while (curr_reporting.load() != rec_tid)
                    ;
                deleted_ids.merge(deleted_ids_local);
                curr_reporting.store((rec_tid + 1) % rec_thd);

                // make a second pass through all pblks
                pthread_barrier_wait(&sync_point);
                if (rec_tid == 0) {
                    itr_raw = _ral->recover(rec_thd);
                }
                pthread_barrier_wait(&sync_point);
                
                thread_local std::vector<PBlk*> not_in_use_local;
                thread_local std::unordered_map<uint64_t, PBlk*> in_use_local;
                for (; !itr_raw[rec_tid].is_last(); ++itr_raw[rec_tid]) {
                    PBlk* curr_blk = (PBlk*)*itr_raw[rec_tid];
                    auto curr_tid = curr_blk->get_tid();
                    auto curr_sn = curr_blk->get_sn();
                    // put all premature pblks and those marked by
                    // deleted_ids in not_in_use
                    if (  // leave DESC blocks untouched for now.
                        curr_blk->blktype != DESC &&
                        // DELETE blocks are already put into anti_nodes_local.
                        curr_blk->blktype != DELETE && (
                            // block without epoch number, probably just inited
                            curr_blk->epoch == NULL_EPOCH ||
                            // premature pblk
                            curr_blk->epoch > epoch_cap ||
                            // marked deleted by some anti-block
                            deleted_ids.find(curr_blk->get_id()) != deleted_ids.end() ||
                            // premature transaction: not registered in descs
                            curr_sn > descs[curr_tid]->get_sn() ||
                            // premature transaction: registered but not committed
                            (curr_sn == descs[curr_tid]->get_sn() && !descs[curr_tid]->committed()))) {
                        not_in_use_local.push_back(curr_blk);
                    } else {
                        // put all others in in_use while resolve conflict
                        switch (curr_blk->blktype) {
                            case OWNED:
                                errexit(
                                    "OWNED isn't a valid blktype in this "
                                    "version.");
                                break;
                            case ALLOC: {
                                auto insert_res =
                                    in_use_local.insert({curr_blk->id, curr_blk});
                                if (insert_res.second == false) {
                                    if (clean_start) {
                                        errexit(
                                            "more than one record with the "
                                            "same id after a clean exit.");
                                    }
                                    not_in_use_local.push_back(curr_blk);
                                }
                            } break;
                            case UPDATE: {
                                auto search = in_use_local.find(curr_blk->id);
                                if (search != in_use_local.end()) {
                                    if (clean_start) {
                                        errexit(
                                            "more than one record with the "
                                            "same id after a clean exit.");
                                    }
                                    if (curr_blk->epoch >
                                        search->second->epoch) {
                                        not_in_use_local.push_back(search->second);
                                        search->second =
                                            curr_blk;  // TODO: double-check if
                                                       // this is right.
                                    } else {
                                        not_in_use_local.push_back(curr_blk);
                                    }
                                } else {
                                    in_use_local.insert({curr_blk->id, curr_blk});
                                }
                            } break;
                            case DELETE:
                            case EPOCH:
                            case DESC:
                                break;
                            default:
                                errexit("wrong type of pblk discovered");
                                break;
                        }
                    }
                }
                // merge the results of in_use, resolve conflict
                pthread_barrier_wait(&sync_point);
                while (curr_reporting.load() != rec_tid)
                    ;
                for (auto itr : in_use_local) {
                    auto found = in_use->find(itr.first);
                    if (found == in_use->end()) {
                        in_use->insert({itr.first, itr.second});
                    } else if (found->second->get_epoch() <
                               itr.second->get_epoch()) {
                        not_in_use_local.push_back(found->second);
                        found->second = itr.second;
                    } else {
                        not_in_use_local.push_back(itr.second);
                    }
                }
                curr_reporting.store((rec_tid + 1) % rec_thd);
                // clean up not_in_use and anti-nodes
                for (auto itr : not_in_use_local) {
                    itr->set_epoch(NULL_EPOCH);
                    _ral->deallocate(itr, rec_tid);
                }
                for (auto itr : anti_nodes_local) {
                    itr->set_epoch(NULL_EPOCH);
                    _ral->deallocate(itr, rec_tid);
                }
            }));  // workers.emplace_back()
        }  // for (rec_thd)
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        // set system mode back to online
        sys_mode = ONLINE;
        reset();

        std::cout << "returning from EpochSys Recovery." << std::endl;
#endif /* !MNEMOSYNE */
        return in_use;
    }


}// namespace pds

#include "ToBePersistedContainers.hpp"
#include "EpochSys.hpp"

using namespace pds;

void rebuild_affinity(GlobalTestConfig* gtc, std::vector<hwloc_obj_t>& persister_affinities){
    // re-build worker thread affinity that pin current threads to individual cores
    // build affinities that pin persisters to hyperthreads of worker threads
    gtc->affinities.clear();
    if (gtc->affinity.compare("interleaved") == 0){
        gtc->buildInterleavedPerCoreAffinity(gtc->affinities, 0);
        gtc->buildInterleavedPerCoreAffinity(persister_affinities, 1);
    } else if (gtc->affinity.compare("singleSocket") == 0){
        gtc->buildSingleSocketPerCoreAffinity(gtc->affinities, 0);
        gtc->buildSingleSocketPerCoreAffinity(persister_affinities, 1);
    } else {
        gtc->buildPerCoreAffinity(gtc->affinities, 0);
        gtc->buildPerCoreAffinity(persister_affinities, 1);
    }
}

// void BufferedWB::PerThreadDedicatedWait::persister_main(int worker_id){
//     // pin this thread to hyperthreads of worker threads.
//     hwloc_set_cpubind(gtc->topology, 
//         persister_affinities[worker_id]->cpuset,HWLOC_CPUBIND_THREAD);
//     // spin until signaled to destruct.
//     int last_signal = 0;
//     while(!exit.load(std::memory_order_acquire)){
//         // wait on worker (tid == worker_id) thread's signal.
//         std::unique_lock<std::mutex> lck(signals[worker_id].bell);
//         signals[worker_id].ring.wait(lck, [&]{return (last_signal != signals[worker_id].curr);});
//         last_signal = signals[worker_id].curr;
//         // dumps
//         for (int i = 0; i < con->dump_size; i++){
//             con->container->try_pop_local(&do_persist, worker_id, signals[worker_id].epoch);
//         }
//     }
// }
// BufferedWB::PerThreadDedicatedWait::PerThreadDedicatedWait(BufferedWB* _con, GlobalTestConfig* _gtc) :
//     Persister(_con), gtc(_gtc) {
//     rebuild_affinity(gtc, persister_affinities);
//     // init environment
//     exit.store(false, std::memory_order_relaxed);
//     signals = new Signal[gtc->task_num];
//     // spawn threads
//     for (int i = 0; i < gtc->task_num; i++){
//         persisters.push_back(std::move(
//             std::thread(&PerThreadDedicatedWait::persister_main, this, i)));
//     }
// }
// BufferedWB::PerThreadDedicatedWait::~PerThreadDedicatedWait(){
//     // signal exit of worker threads.
//     exit.store(true, std::memory_order_release);
//     for (int i = 0; i < gtc->task_num; i++){
//         // TODO: lock here?
//         signals[i].curr++;
//         signals[i].ring.notify_one();
//     }
//     // join threads
//     for (auto i = persisters.begin(); i != persisters.end(); i++){
//         if (i->joinable()){
//             // std::cout<<"joining thread."<<std::endl;
//             i->join();
//             // std::cout<<"joined."<<std::endl;
//         }
//     }
//     delete signals;
// }
// void BufferedWB::PerThreadDedicatedWait::help_persist_local(uint64_t c){
//     // notify hyperthread.
//     {
//         std::unique_lock<std::mutex> lck(signals[EpochSys::tid].bell);
//         signals[EpochSys::tid].curr++;
//         signals[EpochSys::tid].epoch = c;
//     }
//     signals[EpochSys::tid].ring.notify_one();
// }

// void BufferedWB::PerThreadDedicatedBusy::persister_main(int worker_id){
//     // pin this thread to hyperthreads of worker threads.
//     hwloc_set_cpubind(gtc->topology, 
//         persister_affinities[worker_id]->cpuset,HWLOC_CPUBIND_THREAD);
//     // spin until signaled to destruct.
//     int last_signal = 0;
//     int curr_signal = 0;
//     uint64_t curr_epoch = NULL_EPOCH;
//     while(!exit){
//         // wait on worker (tid == worker_id) thread's signal.
//         while(true){
//             if (exit.load(std::memory_order_acquire)){
//                 return;
//             }
//             curr_signal = signals[worker_id].curr.load(std::memory_order_acquire);
//             if (curr_signal != last_signal){
//                 break;
//             }
//         }
//         curr_epoch = signals[worker_id].epoch;
//         // dumps
//         for (int i = 0; i < con->dump_size; i++){
//             con->container->try_pop_local(&do_persist, worker_id, curr_epoch);
//         }
//         signals[worker_id].ack.fetch_add(1, std::memory_order_release);
//         last_signal = curr_signal;
//     }
// }
// BufferedWB::PerThreadDedicatedBusy::PerThreadDedicatedBusy(BufferedWB* _con, GlobalTestConfig* _gtc) :
//     Persister(_con), gtc(_gtc) {
//     rebuild_affinity(gtc, persister_affinities);
//     // init environment
//     exit.store(false, std::memory_order_relaxed);
//     signals = new Signal[gtc->task_num];
//     // spawn threads
//     for (int i = 0; i < gtc->task_num; i++){
//         signals[i].curr.store(0, std::memory_order_relaxed);
//         signals[i].ack.store(0, std::memory_order_relaxed);
//         persisters.push_back(std::move(
//             std::thread(&PerThreadDedicatedBusy::persister_main, this, i)));
//     }
// }
// BufferedWB::PerThreadDedicatedBusy::~PerThreadDedicatedBusy(){
//     // signal exit of worker threads.
//     exit.store(true, std::memory_order_release);
//     // join threads
//     for (auto i = persisters.begin(); i != persisters.end(); i++){
//         if (i->joinable()){
//             i->join();
//         }
//     }
//     delete signals;
// }
// void BufferedWB::PerThreadDedicatedBusy::help_persist_local(uint64_t c){
//     // notify hyperthread.
//     signals[EpochSys::tid].epoch = c;
//     int prev = signals[EpochSys::tid].curr.fetch_add(1, std::memory_order_release);
//     // make sure the persister gets the correct epoch.
//     while(prev == signals[EpochSys::tid].ack.load(std::memory_order_acquire));
// }
// void BufferedWB::WorkerThreadPersister::help_persist_local(uint64_t c){
//     for (int i = 0; i < con->dump_size; i++){
//         con->container->try_pop_local(&do_persist, EpochSys::tid, c);
//     }
// }
void ToBePersistContainer::init_desc_local(void* addr, int tid){
    // Hs: currently we only have descs as per-thread persistent metadata, so
    // recording addrs of descs at init time might seem unecessary.
    // If we will not have more persistent metadata to be persisted at the end
    // of each epoch, consider removing this method and input addr every time
    // we call register_persist_desc_local.
    assert(addr);
    descs_p[tid].ui = addr;
}

void ToBePersistContainer::register_persist_desc_local(uint64_t c, int tid){
    bool f = false;
    desc_persist_indicators[c%EPOCH_WINDOW][tid].ui.compare_exchange_strong(
        f, true);
}

void ToBePersistContainer::do_persist_desc_local(uint64_t c, int tid) {
    void* blk = descs_p[tid].ui;
    if (blk){
        bool t = true;
        persist_func::clwb_range_nofence(blk, ral->malloc_size(blk));
        desc_persist_indicators[c%EPOCH_WINDOW][tid].ui.compare_exchange_strong(t, false);
    }
}

void BufferedWB::do_persist(void*& addr) {
    if (is_raw(addr)){
        persist_func::clwb(unmark_raw(addr));
    } else {
        persist_func::clwb_range_nofence(
            addr, ral->malloc_size(addr));
    }
}
void BufferedWB::register_persist(PBlk* blk, uint64_t c){
    assert(blk!=nullptr);
    if (c == NULL_EPOCH){
        errexit("registering persist of epoch NULL.");
    }
    container->push(blk, [&](void*& addr){do_persist(addr);}, EpochSys::tid, c);
    
}
void BufferedWB::register_persist_raw(PBlk* blk, uint64_t c){
    assert(blk!=nullptr);
    if (c == NULL_EPOCH){
        errexit("registering persist of epoch NULL.");
    }
    container->push(mark_raw(blk), [&](void*& addr){do_persist(addr);}, EpochSys::tid, c);
}
void BufferedWB::persist_epoch(uint64_t c){ // NOTE: this is not thread-safe.
    // for (int i = 0; i < task_num; i++){
    //     container->pop_all_local(&do_persist, i, c);
    // }
    container->pop_all([&](void*& addr){do_persist(addr);}, c);
    for (int i = 0; i < task_num; i++){
        do_persist_desc_local(c, i);
    }
}
void BufferedWB::persist_epoch_local(uint64_t c, int tid){
    container->pop_all_local([&](void*& addr){do_persist(addr);}, tid, c);
    do_persist_desc_local(c, tid);
}
void BufferedWB::clear(){
    container->clear();
}
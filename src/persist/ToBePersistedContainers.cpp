#include "ToBePersistedContainers.hpp"
#include "EpochSys.hpp"

using namespace pds;

void PerEpoch::AdvancerPersister::persist_epoch(uint64_t c){
    con->container->pop_all(&do_persist, c);
}
void PerEpoch::PerThreadDedicatedWait::persister_main(int worker_id){
    // pin this thread to hyperthreads of worker threads.
    hwloc_set_cpubind(gtc->topology, 
        persister_affinities[worker_id]->cpuset,HWLOC_CPUBIND_THREAD);
    // spin until signaled to destruct.
    int curr_epoch = INIT_EPOCH;
    while(!exit){
        // wait on worker (tid == worker_id) thread's signal.
        // NOTE: lock here provides an sfence for epoch boundary
        std::unique_lock<std::mutex> lck(signal.bell);
        signal.ring.wait(lck, [&]{return (curr_epoch != signal.epoch);});
        curr_epoch = signal.epoch;
        // dumps
        con->container->pop_all_local(&do_persist, worker_id, curr_epoch);
        // increment finish_counter
        signal.finish_counter.fetch_add(1, std::memory_order_release);
    }
}
PerEpoch::PerThreadDedicatedWait::PerThreadDedicatedWait(PerEpoch* _con, GlobalTestConfig* _gtc) :
    Persister(_con), gtc(_gtc) {
    // re-build worker thread affinity that pin current threads to individual cores
    gtc->affinities.clear();
    gtc->buildPerCoreAffinity(gtc->affinities, 0);
    // build affinities that pin persisters to hyperthreads of worker threads
    gtc->buildPerCoreAffinity(persister_affinities, 1);
    // init environment
    exit.store(false, std::memory_order_relaxed);
    // spawn threads
    for (int i = 0; i < gtc->task_num; i++){
        persisters.push_back(std::move(
            std::thread(&PerThreadDedicatedWait::persister_main, this, i)));
    }
}
PerEpoch::PerThreadDedicatedWait::~PerThreadDedicatedWait(){
    // signal exit of worker threads.
    exit.store(true, std::memory_order_release);
    signal.ring.notify_all();
    // join threads
    for (auto i = persisters.begin(); i != persisters.end(); i++){
        if (i->joinable()){
            i->join();
        }
    }
}
void PerEpoch::do_persist(std::pair<void*, size_t>& addr_size){
    persist_func::clwb_range_nofence(
        addr_size.first, addr_size.second);
}
void PerEpoch::PerThreadDedicatedWait::persist_epoch(uint64_t c){
    assert(c > last_persisted);
    // set finish_counter to 0.
    signal.finish_counter.store(0, std::memory_order_release);
    // notify hyperthreads.
    {
        std::unique_lock<std::mutex> lck(signal.bell);
        signal.epoch = c;
    }
    signal.ring.notify_all();
    // wait here until persisters finish.
    while(signal.finish_counter.load(std::memory_order_acquire) < gtc->task_num);
    last_persisted = c;
}

void PerEpoch::register_persist(PBlk* blk, size_t sz, uint64_t c){
    if (c == NULL_EPOCH){
        errexit("registering persist of epoch NULL.");
    }
    container->push(std::make_pair<void*, size_t>((char*)blk, (size_t)sz), EpochSys::tid, c);
}
void PerEpoch::register_persist_raw(PBlk* blk, uint64_t c){
    container->push(std::make_pair<void*, size_t>((char*)blk, 1), EpochSys::tid, c);
}
void PerEpoch::persist_epoch(uint64_t c){
    persister->persist_epoch(c);
}
void PerEpoch::clear(){
    container->clear();
}

void BufferedWB::PerThreadDedicatedWait::persister_main(int worker_id){
    // pin this thread to hyperthreads of worker threads.
    hwloc_set_cpubind(gtc->topology, 
        persister_affinities[worker_id]->cpuset,HWLOC_CPUBIND_THREAD);
    // spin until signaled to destruct.
    int last_signal = 0;
    int curr_signal = 0;
    uint64_t curr_epoch = NULL_EPOCH;

    while(!exit){
        // wait on worker (tid == worker_id) thread's signal.
        std::unique_lock<std::mutex> lck(signals[worker_id].bell);
        while(last_signal == curr_signal && !exit){
            curr_signal = signals[worker_id].curr;
            signals[worker_id].ring.wait(lck);
            curr_epoch = signals[worker_id].epoch;
        }
        last_signal = curr_signal;
        // dumps
        for (int i = 0; i < con->dump_size; i++){
            con->container->try_pop_local(&do_persist, worker_id, curr_epoch);
        }
    }
}
BufferedWB::PerThreadDedicatedWait::PerThreadDedicatedWait(BufferedWB* _con, GlobalTestConfig* _gtc) :
    Persister(_con), gtc(_gtc) {
    // re-build worker thread affinity that pin current threads to individual cores
    gtc->affinities.clear();
    gtc->buildPerCoreAffinity(gtc->affinities, 0);
    // build affinities that pin persisters to hyperthreads of worker threads
    gtc->buildPerCoreAffinity(persister_affinities, 1);
    // init environment
    exit.store(false, std::memory_order_relaxed);
    signals = new Signal[gtc->task_num];
    // spawn threads
    for (int i = 0; i < gtc->task_num; i++){
        persisters.push_back(std::move(
            std::thread(&PerThreadDedicatedWait::persister_main, this, i)));
    }
}
BufferedWB::PerThreadDedicatedWait::~PerThreadDedicatedWait(){
    // signal exit of worker threads.
    exit.store(true, std::memory_order_release);
    for (int i = 0; i < gtc->task_num; i++){
        // TODO: lock here?
        signals[i].curr++;
        signals[i].ring.notify_one();
    }
    // join threads
    for (auto i = persisters.begin(); i != persisters.end(); i++){
        if (i->joinable()){
            // std::cout<<"joining thread."<<std::endl;
            i->join();
            // std::cout<<"joined."<<std::endl;
        }
    }
    delete signals;
}
void BufferedWB::PerThreadDedicatedWait::help_persist_local(uint64_t c){
    // notify hyperthread.
    {
        std::unique_lock<std::mutex> lck(signals[EpochSys::tid].bell);
        signals[EpochSys::tid].curr++;
        signals[EpochSys::tid].epoch = c;
    }
    signals[EpochSys::tid].ring.notify_one();
}

void BufferedWB::PerThreadDedicatedBusy::persister_main(int worker_id){
    // pin this thread to hyperthreads of worker threads.
    hwloc_set_cpubind(gtc->topology, 
        persister_affinities[worker_id]->cpuset,HWLOC_CPUBIND_THREAD);
    // spin until signaled to destruct.
    int last_signal = 0;
    int curr_signal = 0;
    uint64_t curr_epoch = NULL_EPOCH;
    while(!exit){
        // wait on worker (tid == worker_id) thread's signal.
        while(true){
            if (exit.load(std::memory_order_acquire)){
                return;
            }
            curr_signal = signals[worker_id].curr.load(std::memory_order_acquire);
            if (curr_signal != last_signal){
                break;
            }
        }
        curr_epoch = signals[worker_id].epoch;
        // dumps
        for (int i = 0; i < con->dump_size; i++){
            con->container->try_pop_local(&do_persist, worker_id, curr_epoch);
        }
        signals[worker_id].ack.fetch_add(1, std::memory_order_release);
        last_signal = curr_signal;
    }
}
BufferedWB::PerThreadDedicatedBusy::PerThreadDedicatedBusy(BufferedWB* _con, GlobalTestConfig* _gtc) :
    Persister(_con), gtc(_gtc) {
    // re-build worker thread affinity that pin current threads to individual cores
    gtc->affinities.clear();
    gtc->buildPerCoreAffinity(gtc->affinities, 0);
    // build affinities that pin persisters to hyperthreads of worker threads
    gtc->buildPerCoreAffinity(persister_affinities, 1);
    // init environment
    exit.store(false, std::memory_order_relaxed);
    signals = new Signal[gtc->task_num];
    // spawn threads
    for (int i = 0; i < gtc->task_num; i++){
        signals[i].curr.store(0, std::memory_order_relaxed);
        signals[i].ack.store(0, std::memory_order_relaxed);
        persisters.push_back(std::move(
            std::thread(&PerThreadDedicatedBusy::persister_main, this, i)));
    }
}
BufferedWB::PerThreadDedicatedBusy::~PerThreadDedicatedBusy(){
    // signal exit of worker threads.
    exit.store(true, std::memory_order_release);
    // join threads
    for (auto i = persisters.begin(); i != persisters.end(); i++){
        if (i->joinable()){
            i->join();
        }
    }
    delete signals;
}
void BufferedWB::PerThreadDedicatedBusy::help_persist_local(uint64_t c){
    // notify hyperthread.
    signals[EpochSys::tid].epoch = c;
    int prev = signals[EpochSys::tid].curr.fetch_add(1, std::memory_order_release);
    // make sure the persister gets the correct epoch.
    while(prev == signals[EpochSys::tid].ack.load(std::memory_order_acquire));
}
void BufferedWB::WorkerThreadPersister::help_persist_local(uint64_t c){
    for (int i = 0; i < con->dump_size; i++){
        con->container->try_pop_local(&do_persist, EpochSys::tid, c);
    }
}
void BufferedWB::do_persist(std::pair<void*, size_t>& addr_size){
    persist_func::clwb_range_nofence(
        addr_size.first, addr_size.second);
}
void BufferedWB::dump(uint64_t c){
    for (int i = 0; i < dump_size; i++){
        container->try_pop_local(&do_persist, EpochSys::tid, c);
    }
}
void BufferedWB::push(std::pair<void*, size_t> entry, uint64_t c){
    while (!container->try_push(entry, EpochSys::tid, c)){// in case other thread(s) are doing write-backs.
        persister->help_persist_local(c);
    }
}
void BufferedWB::register_persist(PBlk* blk, size_t sz, uint64_t c){
    if (c == NULL_EPOCH){
        errexit("registering persist of epoch NULL.");
    }
    push(std::make_pair<void*, size_t>((char*)blk, (size_t)sz), c);
    
}
void BufferedWB::register_persist_raw(PBlk* blk, uint64_t c){
    if (c == NULL_EPOCH){
        errexit("registering persist of epoch NULL.");
    }
    push(std::make_pair<void*, size_t>((char*)blk, 1), c);
}
void BufferedWB::persist_epoch(uint64_t c){ // NOTE: this is not thread-safe.
    for (int i = 0; i < task_num; i++){
        container->pop_all_local(&do_persist, i, c);
    }
}
void BufferedWB::clear(){
    container->clear();
}
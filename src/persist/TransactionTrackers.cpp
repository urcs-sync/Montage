#include "TransactionTrackers.hpp"
#include "EpochSys.hpp"

namespace pds{

bool PerEpochTransactionTracker::consistent_set(uint64_t target, uint64_t c){
    assert(EpochSys::tid != -1);
    curr_epochs[EpochSys::tid].ui.store(target, std::memory_order_seq_cst); // fence
    if (c == global_epoch->load(std::memory_order_acquire)){
        return true;
    } else {
        curr_epochs[EpochSys::tid].ui.store(NULL_EPOCH, std::memory_order_seq_cst); // TODO: double-check this fence.
        return false;
    }
}
PerEpochTransactionTracker::PerEpochTransactionTracker(atomic<uint64_t>* ge, int tn): TransactionTracker(ge), task_num(tn){
    curr_epochs = new paddedAtomic<uint64_t>[task_num];
    for (int i = 0; i < task_num; i++){
        curr_epochs[i].ui.store(NULL_EPOCH);
    }
}
bool PerEpochTransactionTracker::consistent_register_active(uint64_t target, uint64_t c){
    return consistent_set(target, c);
}
bool PerEpochTransactionTracker::consistent_register_bookkeeping(uint64_t target, uint64_t c){
    return consistent_set(target, c);
}
void PerEpochTransactionTracker::unregister_active(uint64_t target){
    assert(EpochSys::tid != -1);
    curr_epochs[EpochSys::tid].ui.store(NULL_EPOCH, std::memory_order_seq_cst);
}
void PerEpochTransactionTracker::unregister_bookkeeping(uint64_t target){
    assert(EpochSys::tid != -1);
    curr_epochs[EpochSys::tid].ui.store(NULL_EPOCH, std::memory_order_seq_cst);
}
bool PerEpochTransactionTracker::no_active(uint64_t target){
    for (int i = 0; i < task_num; i++){
        uint64_t curr_epoch = curr_epochs[i].ui.load(std::memory_order_acquire);
        if (target == curr_epoch && curr_epoch != NULL_EPOCH){
            // std::cout<<"target:"<<target<<" curr_epoch:"<<curr_epoch<<" i:"<<i<<std::endl;
            return false;
        }
    }
    return true;
}
bool PerEpochTransactionTracker::no_bookkeeping(uint64_t target){
    return no_active(target);
}
void PerEpochTransactionTracker::finalize(){
    for (int i = 0; i < task_num; i++){
        curr_epochs[i].ui.store(NULL_EPOCH);
    }
}



bool AtomicTransactionTracker::consistent_increment(std::atomic<uint64_t>& counter, const uint64_t c){
    counter.fetch_add(1, std::memory_order_seq_cst);
    if (c == global_epoch->load(std::memory_order_seq_cst)){
        return true;
    } else {
        counter.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
}
AtomicTransactionTracker::AtomicTransactionTracker(atomic<uint64_t>* ge): TransactionTracker(ge){
    for (int i = 0; i < 4; i++){
        active_transactions[i].ui.store(0, std::memory_order_relaxed);
        bookkeeping_transactions[i].ui.store(0, std::memory_order_relaxed);
    }
}
bool AtomicTransactionTracker::consistent_register_active(uint64_t target, uint64_t c){
    return consistent_increment(active_transactions[target%4].ui, c);
}
bool AtomicTransactionTracker::consistent_register_bookkeeping(uint64_t target, uint64_t c){
    return consistent_increment(bookkeeping_transactions[target%4].ui, c);
}
void AtomicTransactionTracker::unregister_active(uint64_t target){
    active_transactions[target%4].ui.fetch_sub(1, std::memory_order_seq_cst);
}
void AtomicTransactionTracker::unregister_bookkeeping(uint64_t target){
    bookkeeping_transactions[target%4].ui.fetch_sub(1, std::memory_order_seq_cst);
}
bool AtomicTransactionTracker::no_active(uint64_t target){
    return (active_transactions[target%4].ui.load(std::memory_order_seq_cst) == 0);
}
bool AtomicTransactionTracker::no_bookkeeping(uint64_t target){
    return (bookkeeping_transactions[target%4].ui.load(std::memory_order_seq_cst) == 0);
}


void NoFenceTransactionTracker::set_register(paddedAtomic<bool>* indicators){
    assert(EpochSys::tid != -1);
    indicators[EpochSys::tid].ui.store(true, std::memory_order_release);
}
void NoFenceTransactionTracker::set_unregister(paddedAtomic<bool>* indicators){
    assert(EpochSys::tid != -1);
    indicators[EpochSys::tid].ui.store(false, std::memory_order_release);
}
bool NoFenceTransactionTracker::consistent_register(paddedAtomic<bool>* indicators, const uint64_t c){
    set_register(indicators);
    if (c == global_epoch->load(std::memory_order_acquire)){
        return true;
    } else {
        // Hs: I guess we don't ever need a fence here.
        assert(EpochSys::tid != -1);
        indicators[EpochSys::tid].ui.store(false, std::memory_order_release);
        return false;
    }
}
bool NoFenceTransactionTracker::all_false(paddedAtomic<bool>* indicators){
    for (int i = 0; i < task_num; i++){
        if (indicators[i].ui.load(std::memory_order_acquire) == true){
            return false;
        }
    }
    return true;
}
NoFenceTransactionTracker::NoFenceTransactionTracker(atomic<uint64_t>* ge, int tn):
    TransactionTracker(ge), task_num(tn){
    for (int i = 0; i < 4; i++){
        active_transactions[i].ui = new paddedAtomic<bool>[task_num];
        bookkeeping_transactions[i].ui = new paddedAtomic<bool>[task_num];
        for (int j = 0; j < task_num; j++){
            active_transactions[i].ui[j].ui.store(false);
            bookkeeping_transactions[i].ui[j].ui.store(false);
        }
    }
}
bool NoFenceTransactionTracker::consistent_register_active(uint64_t target, uint64_t c){
    return consistent_register(active_transactions[target%4].ui, c);
}
bool NoFenceTransactionTracker::consistent_register_bookkeeping(uint64_t target, uint64_t c){
    return consistent_register(bookkeeping_transactions[target%4].ui, c);
}
void NoFenceTransactionTracker::unregister_active(uint64_t target){
    set_unregister(active_transactions[target%4].ui);
}
void NoFenceTransactionTracker::unregister_bookkeeping(uint64_t target){
    set_unregister(bookkeeping_transactions[target%4].ui);
}
bool NoFenceTransactionTracker::no_active(uint64_t target){
    return all_false(active_transactions[target%4].ui);
}
bool NoFenceTransactionTracker::no_bookkeeping(uint64_t target){
    return all_false(bookkeeping_transactions[target%4].ui);
}

void FenceBeginTransactionTracker::set_register(paddedAtomic<bool>* indicators){
    assert(EpochSys::tid != -1);
    indicators[EpochSys::tid].ui.store(true, std::memory_order_seq_cst);
}
FenceBeginTransactionTracker::FenceBeginTransactionTracker(atomic<uint64_t>* ge, int task_num):
    NoFenceTransactionTracker(ge, task_num){}

void FenceEndTransactionTracker::set_unregister(paddedAtomic<bool>* indicators){
    assert(EpochSys::tid != -1);
    indicators[EpochSys::tid].ui.store(false, std::memory_order_seq_cst);
}
FenceEndTransactionTracker::FenceEndTransactionTracker(atomic<uint64_t>* ge, int task_num):
    NoFenceTransactionTracker(ge, task_num){}

}
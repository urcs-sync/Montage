#include "ToBeFreedContainers.hpp"

#include "EpochSys.hpp"

using namespace pds;

void PerThreadFreedContainer::do_free(PBlk*& x){
    delete x;
}
PerThreadFreedContainer::PerThreadFreedContainer(GlobalTestConfig* gtc): task_num(gtc->task_num){
    container = new VectorContainer<PBlk*>(gtc->task_num);
    threadEpoch = new padded<uint64_t>[gtc->task_num];
    locks = new padded<std::mutex>[gtc->task_num];
    for(int i = 0; i < gtc->task_num; i++){
        threadEpoch[i] = NULL_EPOCH;
    }
}
PerThreadFreedContainer::~PerThreadFreedContainer(){
    delete container;
}
void PerThreadFreedContainer::free_on_new_epoch(uint64_t c){
    /* there are 3 possilibities:
        1. thread's previous transaction epoch is c, in this case, just return
        2. thread's previous transaction epoch is c-1, in this case, free the retired blocks in epoch c-2, and update the thread's
            most recent transaction epoch number
        3. thread's previous transaction epoch is smaller than c-1, in this case, just return, because epoch advanver has already
            freed all the blocks from 2 epochs ago, then update the thread's most recent transaction epoch number
        So we need to keep the to_be_free->help_free(c-2) in epoch_advancer. */

    if( c == threadEpoch[EpochSys::tid] -1){
        std::lock_guard<std::mutex> lk(locks[EpochSys::tid].ui);
        help_free_local(c - 2);
        threadEpoch[EpochSys::tid] = c;
    }else if( c < threadEpoch[EpochSys::tid] -1){
        threadEpoch[EpochSys::tid] = c;
    }
}
void PerThreadFreedContainer::register_free(PBlk* blk, uint64_t c){
    // container[c%4].ui->push(blk, EpochSys::tid);
    container->push(blk, EpochSys::tid, c);
}
void PerThreadFreedContainer::help_free(uint64_t c){
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
void PerThreadFreedContainer::help_free_local(uint64_t c){
    container->pop_all_local(&do_free, EpochSys::tid, c);
}
void PerThreadFreedContainer::clear(){
    container->clear();
}


void PerEpochFreedContainer::do_free(PBlk*& x){
    delete x;
}
PerEpochFreedContainer::PerEpochFreedContainer(GlobalTestConfig* gtc){
    container = new VectorContainer<PBlk*>(gtc->task_num);
    // container = new HashSetContainer<PBlk*>(gtc->task_num);
}
PerEpochFreedContainer::~PerEpochFreedContainer(){
    delete container;
}
void PerEpochFreedContainer::register_free(PBlk* blk, uint64_t c){
    // container[c%4].ui->push(blk, EpochSys::tid);
    container->push(blk, EpochSys::tid, c);
}
void PerEpochFreedContainer::help_free(uint64_t c){
    container->pop_all(&do_free, c);
}
void PerEpochFreedContainer::help_free_local(uint64_t c){
    container->pop_all_local(&do_free, EpochSys::tid, c);
}
void PerEpochFreedContainer::clear(){
    container->clear();
}

void NoToBeFreedContainer::register_free(PBlk* blk, uint64_t c){
    delete blk;
}

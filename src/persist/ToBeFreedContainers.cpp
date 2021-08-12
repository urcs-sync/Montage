#include "ToBeFreedContainers.hpp"

#include "EpochSys.hpp"

using namespace pds;

void ThreadLocalFreedContainer::do_free(PBlk*& x, uint64_t c){
    _esys->delete_pblk(x, c);
}
ThreadLocalFreedContainer::ThreadLocalFreedContainer(EpochSys* e, GlobalTestConfig* gtc): task_num(gtc->task_num){
    container = new VectorContainer<PBlk*>(gtc->task_num);
    threadEpoch = new padded<uint64_t>[gtc->task_num];
    _esys = e;
    for(int i = 0; i < gtc->task_num; i++){
        threadEpoch[i] = INIT_EPOCH;
    }
}
ThreadLocalFreedContainer::~ThreadLocalFreedContainer(){
    delete container;
}
void ThreadLocalFreedContainer::free_on_new_epoch(uint64_t c){
    auto last_epoch = threadEpoch[EpochSys::tid].ui;
    if (last_epoch == c){
        return;
    }
    threadEpoch[EpochSys::tid].ui = c;
    for (uint64_t i = last_epoch-1;
        i <= min(last_epoch+1, c-2); i++){
        help_free_local(i);
        persist_func::sfence();
    }
}
void ThreadLocalFreedContainer::register_free(PBlk* blk, uint64_t c){
    assert(blk!=nullptr);
    // container[c%4].ui->push(blk, EpochSys::tid);
    container->push(blk, EpochSys::tid, c);
}
void ThreadLocalFreedContainer::help_free(uint64_t c){
    // do nothing. all frees should be done by worker threads.
}
void ThreadLocalFreedContainer::help_free_local(uint64_t c){
    container->pop_all_local([&,this](PBlk*& x){this->do_free(x, c);}, EpochSys::tid, c);
}
void ThreadLocalFreedContainer::clear(){
    container->clear();
}


void PerEpochFreedContainer::do_free(PBlk*& x, uint64_t c){
    _esys->delete_pblk(x, c);
}
PerEpochFreedContainer::PerEpochFreedContainer(EpochSys* e, GlobalTestConfig* gtc){
    container = new VectorContainer<PBlk*>(gtc->task_num);
    _esys = e;
    // container = new HashSetContainer<PBlk*>(gtc->task_num);
}
PerEpochFreedContainer::~PerEpochFreedContainer(){
    delete container;
}
void PerEpochFreedContainer::register_free(PBlk* blk, uint64_t c){
    assert(blk!=nullptr);
    // container[c%4].ui->push(blk, EpochSys::tid);
    container->push(blk, EpochSys::tid, c);
}
void PerEpochFreedContainer::help_free(uint64_t c){
    container->pop_all([&,this](PBlk*& x){this->do_free(x, c);}, c);
}
void PerEpochFreedContainer::help_free_local(uint64_t c){
    container->pop_all_local([&,this](PBlk*& x){this->do_free(x, c);}, EpochSys::tid, c);
}
void PerEpochFreedContainer::clear(){
    container->clear();
}

void NoToBeFreedContainer::register_free(PBlk* blk, uint64_t c){
    assert(blk!=nullptr);
    _esys->delete_pblk(blk, c);
}

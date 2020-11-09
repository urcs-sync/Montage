#include "Recoverable.hpp"

Recoverable::Recoverable(GlobalTestConfig* gtc){
    // init Persistent allocator
    Persistent::init();
    // init epoch system
    pds::init(gtc);
    // init main thread
    pds::init_thread(0);

    // TODO: replace this with _esys initialization.
    _esys = pds::esys;
}
Recoverable::~Recoverable(){
    pds::finalize();
    Persistent::finalize();
}
void Recoverable::init_thread(GlobalTestConfig*, LocalTestConfig* ltc){
    pds::init_thread(ltc->tid);
}
void Recoverable::register_alloc_pblk(pds::PBlk* pblk){
    _esys->register_alloc_pblk(pblk, _esys->epochs[pds::EpochSys::tid].ui);
}
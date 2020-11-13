#include "Recoverable.hpp"

Recoverable::Recoverable(GlobalTestConfig* gtc){
    // init Persistent allocator
    Persistent::init();
    // init epoch system
    pds::init(gtc);
    // init main thread
    pds::init_thread(0);

    local_descs = new padded<pds::sc_desc_t>[gtc->task_num];
    // TODO: replace this with _esys initialization.
    _esys = pds::esys;
}
Recoverable::~Recoverable(){
    pds::finalize();
    Persistent::finalize();
}
void Recoverable::init_thread(GlobalTestConfig*, LocalTestConfig* ltc){
    _esys->init_thread(ltc->tid);
}

void Recoverable::init_thread(int tid){
    _esys->init_thread(tid);
}

namespace pds{

    void sc_desc_t::try_complete(Recoverable* ds, uint64_t addr){
        nbptr_t _d = nbptr.load();
        int ret = 0;
        if(_d.val!=addr) return;
        if(in_progress(_d)){
            if(ds->check_epoch(cas_epoch)){
                ret = 2;
                ret |= commit(_d);
            } else {
                ret = 4;
                ret |= abort(_d);
            }
        }
        cleanup(_d);
    }

}
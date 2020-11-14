#include "Recoverable.hpp"

Recoverable::Recoverable(GlobalTestConfig* gtc){
    // init Persistent allocator
    // TODO: put this into EpochSys.
    Persistent::init();
    
    local_descs = new padded<pds::sc_desc_t>[gtc->task_num];
    // init main thread
    pds::EpochSys::init_thread(0);
    // init epoch system
    _esys = new pds::EpochSys(gtc);
}
Recoverable::~Recoverable(){
    delete _esys;
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
        lin_var _d = var.load();
        // int ret = 0;
        if(_d.val!=addr) return;
        if(in_progress(_d)){
            if(ds->check_epoch(cas_epoch)){
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

}
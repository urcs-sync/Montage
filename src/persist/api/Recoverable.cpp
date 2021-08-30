#include "Recoverable.hpp"
#include "PersistFunc.hpp"
// std::atomic<size_t> pds::abort_cnt(0);
// std::atomic<size_t> pds::total_cnt(0);
Recoverable::Recoverable(GlobalTestConfig* gtc){
    // init Persistent allocator
    // TODO: put this into EpochSys.
    // Persistent::init();
    epochs = new padded<uint64_t>[gtc->task_num];
    last_epochs = new padded<uint64_t>[gtc->task_num];
    for(int i = 0; i < gtc->task_num; i++){
        epochs[i].ui = NULL_EPOCH;
        last_epochs[i].ui = NULL_EPOCH;
    }
    pending_allocs = new padded<std::vector<pds::PBlk*>>[gtc->task_num];
    pending_retires = new padded<std::vector<pair<pds::PBlk*,pds::PBlk*>>>[gtc->task_num];
    // init main thread
    pds::EpochSys::init_thread(0);
    // init epoch system
    if(gtc->checkEnv("Liveness")){
        string env_liveness = gtc->getEnv("Liveness");
        if(env_liveness == "Nonblocking"){
            _esys = new pds::nbEpochSys(gtc);
        } else if (env_liveness == "Blocking"){
            _esys = new pds::EpochSys(gtc);
        } else {
            errexit("unrecognized 'Liveness' environment");
        }
    } else {
        gtc->setEnv("Liveness", "Blocking");
        _esys = new pds::EpochSys(gtc);
    }
}
Recoverable::~Recoverable(){
    delete _esys;
    delete pending_allocs;
    delete pending_retires;
    delete epochs;
    delete last_epochs;
    Persistent::finalize();
}
void Recoverable::init_thread(GlobalTestConfig*, LocalTestConfig* ltc){
    pds::EpochSys::init_thread(ltc->tid);
}

void Recoverable::init_thread(int tid){
    pds::EpochSys::init_thread(tid);
}

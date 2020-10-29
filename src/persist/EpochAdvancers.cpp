#include "EpochSys.hpp"
#include "EpochAdvancers.hpp"

using namespace pds;

SingleThreadEpochAdvancer::SingleThreadEpochAdvancer(GlobalTestConfig* gtc){
    trans_cnts = new padded<uint64_t>[gtc->task_num];
    for (int i = 0; i < gtc->task_num; i++){
        trans_cnts[i].ui = 0;
    }
}
void SingleThreadEpochAdvancer::set_epoch_freq(int epoch_power){
    epoch_threshold = 0x1ULL << epoch_power;
}
void SingleThreadEpochAdvancer::set_help_freq(int help_power){
    help_threshold = 0x1ULL << help_power;
}
void SingleThreadEpochAdvancer::on_end_transaction(EpochSys* esys, uint64_t c){
    assert(_tid != -1);
    trans_cnts[_tid].ui++;
    if (_tid == 0){
        // only a single thread can advance epochs.
        if (trans_cnts[_tid].ui % epoch_threshold == 0){
            esys->advance_epoch(c);
        } 
    }
}


void GlobalCounterEpochAdvancer::set_epoch_freq(int epoch_power){
    epoch_threshold = 0x1ULL << epoch_power;
}
void GlobalCounterEpochAdvancer::set_help_freq(int help_power){
    help_threshold = 0x1ULL << help_power;
}
void GlobalCounterEpochAdvancer::on_end_transaction(EpochSys* esys, uint64_t c){
    uint64_t curr_cnt = trans_cnt.fetch_add(1, std::memory_order_acq_rel);
    if (curr_cnt % epoch_threshold == 0){
        esys->advance_epoch(c);
    } else if (curr_cnt % help_threshold == 0){
        esys->help_local();
    }
}


DedicatedEpochAdvancer::DedicatedEpochAdvancer(GlobalTestConfig* gtc, EpochSys* es):esys(es){
    if (gtc->checkEnv("EpochLength")){
        epoch_length = stoi(gtc->getEnv("EpochLength"));
    } else {
        epoch_length = 100*1000;
    }
    if (gtc->checkEnv("EpochLengthUnit")){
        std::string env_unit = gtc->getEnv("EpochLengthUnit");
        if (env_unit == "Second"){
            epoch_length *= 1000000;
        } else if (env_unit == "Millisecond"){
            epoch_length *= 1000;
        } else if (env_unit == "Microsecond"){
            // do nothing.
        } else {
            errexit("time unit not supported.");
        }
    }
    started.store(false);
    advancer_thread = std::move(std::thread(&DedicatedEpochAdvancer::advancer, this));
    started.store(true);
}

void DedicatedEpochAdvancer::advancer(){
    while(!started.load()){}
    while(started.load()){
        esys->advance_epoch_dedicated();
        std::this_thread::sleep_for(std::chrono::microseconds(epoch_length));
    }
    // std::cout<<"advancer_thread terminating..."<<std::endl;
}

DedicatedEpochAdvancer::~DedicatedEpochAdvancer(){
    // std::cout<<"terminating advancer_thread"<<std::endl;
    started.store(false);
    if (advancer_thread.joinable()){
        advancer_thread.join();
    }
    // std::cout<<"terminated advancer_thread"<<std::endl;
}

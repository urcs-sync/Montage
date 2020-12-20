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
    assert(EpochSys::tid != -1);
    trans_cnts[EpochSys::tid].ui++;
    if (EpochSys::tid == 0){
        // only a single thread can advance epochs.
        if (trans_cnts[EpochSys::tid].ui % epoch_threshold == 0){
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
    advancer_thread = std::move(std::thread(&DedicatedEpochAdvancer::advancer, this, gtc->task_num));
    started.store(true);
}

void DedicatedEpochAdvancer::advancer(int task_num){
    EpochSys::init_thread(task_num);// set tid to be the last
    while(!started.load()){}
    while(started.load()){
        // esys->advance_epoch_dedicated();
        // std::this_thread::sleep_for(std::chrono::microseconds(epoch_length));

        // wait for sync_queue_signal to fire or timeout
        std::unique_lock<std::mutex> lk(sync_queue_signal.bell);
        sync_queue_signal.ring.wait_for(lk, std::chrono::microseconds(epoch_length), 
            [&]{return (sync_queue_signal.request_cnt.load(std::memory_order_acquire) > 0);});
        // if woke up by sync_queue_signal:
        if (sync_queue_signal.request_cnt.load(std::memory_order_acquire) > 0){
            // go through sync queue
            // does advance for the first entry, pop all the rest with the same epoch
                // for each request, send signal to threads to unblock
            // if request epoch is only 1 larger than prev, advance only once
        } else { // if time's up:
            // advance once.
            esys->advance_epoch_dedicated();
        }
                
    }
    // std::cout<<"advancer_thread terminating..."<<std::endl;
}

void DedicatedEpochAdvancer::sync(uint64_t c){

    // initialize local SyncSignal
    // put SyncSignal into queue
    // notify advancer thread
    // wait for local SyncSignal, return
}

DedicatedEpochAdvancer::~DedicatedEpochAdvancer(){
    // std::cout<<"terminating advancer_thread"<<std::endl;
    started.store(false);
    if (advancer_thread.joinable()){
        advancer_thread.join();
    }
    // std::cout<<"terminated advancer_thread"<<std::endl;
}
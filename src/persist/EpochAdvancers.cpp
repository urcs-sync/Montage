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


DedicatedEpochAdvancer::DedicatedEpochAdvancer(GlobalTestConfig* gtc, EpochSys* es):
    esys(es), local_sync_signals(gtc->task_num){
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
    uint64_t curr_epoch = INIT_EPOCH;
    while(!started.load()){}
    while(started.load()){
        // wait for sync_queue_signal to fire or timeout
        std::unique_lock<std::mutex> lk(sync_queue_signal.bell);
        sync_queue_signal.ring.wait_for(lk, std::chrono::microseconds(epoch_length), 
            [&]{return (sync_queue_signal.request_cnt.load(std::memory_order_acquire) > 0);});
        // if woke up by sync_queue_signal:
        if (sync_queue_signal.request_cnt.load(std::memory_order_acquire) > 0){
            // go through sync queue
            uint64_t last_request = NULL_EPOCH;
            while(sync_queue_signal.request_cnt.load(std::memory_order_acquire) > 0){
                sync_queue_signal.request_cnt.fetch_sub(1, std::memory_order_acq_rel);
                int target_id;
                if (sync_queue.try_pop(target_id)){
                    // does advance for entry with new epoch, pop all the rest with the same epoch
                    uint64_t curr_request = local_sync_signals[target_id].curr_epoch;
                    if (curr_request != last_request){
                        int advance_cnt = (curr_request == curr_epoch? 2 : (curr_request == curr_epoch-1? 1 : 0));
                        for (int i = 0; i < advance_cnt; i++){
                            curr_epoch++;
                            esys->advance_epoch_dedicated();
                        }
                    }
                    // for each request, send signal to the thread to unblock
                    local_sync_signals[target_id].done.store(true);
                    local_sync_signals[target_id].ring.notify_one();
                } else {
                    errexit("request_cnt > 0, but sync_queue empty.");
                    break;
                }
            }
        } else { // if time's up:
            // advance once.
            curr_epoch++;
            esys->advance_epoch_dedicated();
        }
    }
    // std::cout<<"advancer_thread terminating..."<<std::endl;
}

void DedicatedEpochAdvancer::sync(uint64_t c){
    // initialize local SyncSignal
    local_sync_signals[EpochSys::tid].done.store(false, std::memory_order_release);
    local_sync_signals[EpochSys::tid].curr_epoch = c;
    {
        std::unique_lock<std::mutex> lk(local_sync_signals[EpochSys::tid].bell);
        // put SyncSignal into queue
        sync_queue.push(EpochSys::tid);
        // notify advancer thread if necessary
        if (sync_queue_signal.request_cnt.fetch_add(1, std::memory_order_acq_rel) == 0){
            sync_queue_signal.ring.notify_all();
        }
        // wait for local SyncSignal, return
        local_sync_signals[EpochSys::tid].ring.wait(lk, 
            [&]{return local_sync_signals[EpochSys::tid].done.load(std::memory_order_acquire);});
    }
    
}

DedicatedEpochAdvancer::~DedicatedEpochAdvancer(){
    // std::cout<<"terminating advancer_thread"<<std::endl;
    started.store(false);
    if (advancer_thread.joinable()){
        advancer_thread.join();
    }
    // std::cout<<"terminated advancer_thread"<<std::endl;
}
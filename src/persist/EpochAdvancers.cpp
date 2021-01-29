#include "EpochSys.hpp"
#include "EpochAdvancers.hpp"

using namespace pds;


DedicatedEpochAdvancer::DedicatedEpochAdvancer(GlobalTestConfig* gtc, EpochSys* es):
    gtc(gtc), esys(es){
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
    int64_t next_sleep = epoch_length; // unsigned to signed, but should be fine.
    while(!started.load()){}
    while(started.load()){
        if (next_sleep >= 0){
            // wait for sync_signal to fire or timeout
            std::unique_lock<std::mutex> lk(sync_signal.bell);
            if (epoch_length > 0){
                sync_signal.advancer_ring.wait_for(lk, std::chrono::microseconds(next_sleep), 
                    [&]{return (sync_signal.target_epoch > curr_epoch || !started.load());});
            } else {
                sync_signal.advancer_ring.wait(lk,
                    [&]{return (sync_signal.target_epoch > curr_epoch || !started.load());});
            }
            lk.unlock();
        } else {
            // if next_sleep<0, epoch advance is taking longer than an epoch.
            if (gtc->verbose){
                std::cout<<"warning: epoch is getting longer by "<<
                    ((double)abs(next_sleep))/epoch_length << "%" <<std::endl;
            }
        }
        
        if (curr_epoch == sync_signal.target_epoch){
            // no sync singal. advance epoch once.
            sync_signal.target_epoch++;
        }
        
        auto wb_start = chrono::high_resolution_clock::now();

        for (; curr_epoch < sync_signal.target_epoch; curr_epoch++){
            // Wait until all threads active one epoch ago are done
            while(!esys->is_quiesent(curr_epoch-1)){}
            // Persist all modified blocks from 1 epoch ago
            esys->persist_epoch(curr_epoch-1);
            persist_func::sfence();
            // Advance epoch number
            esys->set_epoch(curr_epoch+1);
            // notify worker threads before relamation 
            if (curr_epoch == sync_signal.target_epoch-1){
                // only does notify before last reclamation since notification is somewhat expensive
                sync_signal.worker_ring.notify_all();
            } else {
                // restart timer for a new epoch
                wb_start = chrono::high_resolution_clock::now();
            }
            // does reclamation for curr_epoch-1
            esys->free_epoch(curr_epoch-1);
            
            // if (gtc->verbose){
            //     if (curr_epoch%1024 == 0){
            //         std::cout<<"epoch advanced to:" << curr_epoch+1 <<std::endl;
            //     }
            // }
        }
        
        // measure the time used for write-back and reclamation, and deduct it from epoch_length.
        int64_t wb_length = chrono::duration_cast<chrono::microseconds>(
            chrono::high_resolution_clock::now()-wb_start).count();
        next_sleep = epoch_length - wb_length;
    }
    // std::cout<<"advancer_thread terminating..."<<std::endl;
}

void DedicatedEpochAdvancer::sync(uint64_t c){
    uint64_t target_epoch = c+2;
    std::unique_lock<std::mutex> lk(sync_signal.bell);
    if (target_epoch < sync_signal.target_epoch-2){
        // current epoch is already persisted.
        return;
    }
    if (target_epoch > sync_signal.target_epoch){
        sync_signal.target_epoch = target_epoch;
        sync_signal.advancer_ring.notify_all();
    }
    sync_signal.worker_ring.wait(lk, [&]{return (esys->get_epoch() >= target_epoch);});
}

DedicatedEpochAdvancer::~DedicatedEpochAdvancer(){
    // std::cout<<"terminating advancer_thread"<<std::endl;
    started.store(false);
    // flush and quit dedicated epoch advancer
    {
        std::unique_lock<std::mutex> lk(sync_signal.bell);
        sync_signal.target_epoch += 4;
    }
    sync_signal.advancer_ring.notify_all();
    if (advancer_thread.joinable()){
        advancer_thread.join();
    }
    // std::cout<<"terminated advancer_thread"<<std::endl;
}
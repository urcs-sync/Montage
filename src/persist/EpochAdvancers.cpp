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
    if (!gtc->checkEnv("NoAdvancerPinning")){
        find_first_socket();
    }
    advancer_state.store(INIT);
    advancer_thread = std::move(std::thread(&DedicatedEpochAdvancer::advancer, this, gtc->task_num));
    advancer_state.store(RUNNING);
}

void DedicatedEpochAdvancer::find_first_socket(){
    hwloc_obj_t obj = hwloc_get_root_obj(gtc->topology);
    while(obj->type < HWLOC_OBJ_SOCKET){
        obj = obj->children[0];
    }
    advancer_affinity = obj;
}

void DedicatedEpochAdvancer::advancer(int task_num){
    if (advancer_affinity != nullptr){
        hwloc_set_cpubind(gtc->topology, 
        advancer_affinity->cpuset,HWLOC_CPUBIND_THREAD);
    }
    EpochSys::init_thread(task_num);// set tid to be the last
    uint64_t curr_epoch = INIT_EPOCH;
    int64_t next_sleep = epoch_length; // unsigned to signed, but should be fine.
    while(advancer_state.load() == INIT){}
    while(advancer_state.load() == RUNNING){
        if (next_sleep >= 0){
            if (epoch_length > 0){
                std::this_thread::sleep_for(std::chrono::microseconds(next_sleep));
            }
        } else {
            // if next_sleep<0, epoch advance is taking longer than an epoch.
            if (gtc->verbose){
                std::cout<<"warning: epoch is getting longer by "<<
                    ((double)abs(next_sleep))/epoch_length << "%" <<std::endl;
            }
        }
        
        auto wb_start = chrono::high_resolution_clock::now();

        {
            auto tmp_curr_epoch = curr_epoch;
            // failure is harmless, but failure will modify the first parameter
            target_epoch.ui.compare_exchange_strong(tmp_curr_epoch, tmp_curr_epoch+1); 
            esys->on_epoch_end(curr_epoch);
            // Advance epoch number
            if(esys->epoch_CAS(curr_epoch, curr_epoch+1)){
                curr_epoch++;
                esys->on_epoch_begin(curr_epoch);
            }
        }
        
        // measure the time used for write-back and reclamation, and deduct it from epoch_length.
        int64_t wb_length = chrono::duration_cast<chrono::microseconds>(
            chrono::high_resolution_clock::now()-wb_start).count();
        next_sleep = epoch_length - wb_length;
    }
    // std::cout<<"advancer_thread terminating..."<<std::endl;
}

uint64_t DedicatedEpochAdvancer::ongoing_target() {
    return target_epoch.ui.load();
}

void DedicatedEpochAdvancer::sync(uint64_t c){
    uint64_t curr_target = target_epoch.ui.load();
    while(curr_target < c+2){
        if (target_epoch.ui.compare_exchange_strong(curr_target, c+2)){
            break;
        }
    }
    for (auto curr_epoch=esys->get_epoch(); curr_epoch < c+2; curr_epoch++){
        esys->on_epoch_end(curr_epoch);
        // Advance epoch number
        if (esys->epoch_CAS(curr_epoch, curr_epoch+1)){
            esys->on_epoch_begin(curr_epoch+1);
        }
    }
}

DedicatedEpochAdvancer::~DedicatedEpochAdvancer(){
    // std::cout<<"terminating advancer_thread"<<std::endl;
    advancer_state.store(ENDED);
    sync(esys->get_epoch());
    sync(esys->get_epoch());
    if (advancer_thread.joinable()){
        advancer_thread.join();
    }
    // std::cout<<"terminated advancer_thread"<<std::endl;
}

DedicatedEpochAdvancerNbSync::DedicatedEpochAdvancerNbSync(GlobalTestConfig* gtc, EpochSys* es):
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
    if (!gtc->checkEnv("NoAdvancerPinning")){
        find_first_socket();
    }
    target_epoch.ui.store(INIT_EPOCH);
    if(epoch_length!=0){
        // spawn epoch advancer thread only if epoch length isn't 0
        started.store(false);
        advancer_thread = std::move(std::thread(&DedicatedEpochAdvancerNbSync::advancer, this, gtc->task_num));
        started.store(true);
    }
}

void DedicatedEpochAdvancerNbSync::find_first_socket(){
    hwloc_obj_t obj = hwloc_get_root_obj(gtc->topology);
    while(obj->type < HWLOC_OBJ_SOCKET){
        obj = obj->children[0];
    }
    advancer_affinity = obj;
}

void DedicatedEpochAdvancerNbSync::advancer(int task_num){
    if (advancer_affinity != nullptr){
        hwloc_set_cpubind(gtc->topology, 
        advancer_affinity->cpuset,HWLOC_CPUBIND_THREAD);
    }
    EpochSys::init_thread(task_num);// set tid to be the last
    uint64_t curr_epoch = esys->get_epoch();
    int64_t next_sleep = epoch_length; // unsigned to signed, but should be fine.
    while(!started.load()){}
    while(started.load()){
        if (next_sleep >= 0){
            if (epoch_length > 0){
                std::this_thread::sleep_for(std::chrono::microseconds(next_sleep));
            }
        } else {
            // if next_sleep<0, epoch advance is taking longer than an epoch.
            if (gtc->verbose){
                std::cout<<"warning: epoch is getting longer by "<<
                    ((double)abs(next_sleep))/epoch_length << "%" <<std::endl;
            }
        }
        
        auto wb_start = chrono::high_resolution_clock::now();

        {
            auto tmp_curr_epoch = curr_epoch;
            // failure is harmless, but failure will modify the first parameter
            target_epoch.ui.compare_exchange_strong(tmp_curr_epoch, tmp_curr_epoch+1); 
            esys->on_epoch_end(curr_epoch);
            // Advance epoch number
            if(esys->epoch_CAS(curr_epoch, curr_epoch+1))
                curr_epoch++;
            // restart timer for a new epoch
            wb_start = chrono::high_resolution_clock::now(); // TODO: is this correct?
            esys->on_epoch_begin(curr_epoch+1);// noop in nbEpochSys
        }
        
        // measure the time used for write-back and reclamation, and deduct it from epoch_length.
        int64_t wb_length = chrono::duration_cast<chrono::microseconds>(
            chrono::high_resolution_clock::now()-wb_start).count();
        next_sleep = epoch_length - wb_length;
    }
    // std::cout<<"advancer_thread terminating..."<<std::endl;
}

uint64_t DedicatedEpochAdvancerNbSync::ongoing_target(){
    return target_epoch.ui.load();
}

void DedicatedEpochAdvancerNbSync::sync(uint64_t c){
    uint64_t curr_target = target_epoch.ui.load();
    while(curr_target < c+2){
        if (target_epoch.ui.compare_exchange_strong(curr_target, c+2)){
            break;
        }
    }
    for (auto curr_epoch=esys->get_epoch(); curr_epoch < c+2; curr_epoch++){
        esys->on_epoch_end(curr_epoch);
        // Advance epoch number
        esys->epoch_CAS(curr_epoch, curr_epoch+1);
        esys->on_epoch_begin(curr_epoch+1);// noop in nbEpochSys
    }
}

DedicatedEpochAdvancerNbSync::~DedicatedEpochAdvancerNbSync(){
    // std::cout<<"terminating advancer_thread"<<std::endl;
    started.store(false);
    // flush and quit dedicated epoch advancer
    if (advancer_thread.joinable()){
        advancer_thread.join();
    }
    // std::cout<<"terminated advancer_thread"<<std::endl;
}
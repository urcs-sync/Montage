#ifndef EPOCHADVANCERS_HPP
#define EPOCHADVANCERS_HPP

#include <atomic>
#include <thread>
#include "TestConfig.hpp"
#include "ConcurrentPrimitives.hpp"

namespace pds{

class EpochSys;

/////////////////////
// Epoch Advancers //
/////////////////////

class EpochAdvancer{
public:
    virtual void set_epoch_freq(int epoch_freq) = 0;
    virtual void set_help_freq(int help_freq) = 0;
    virtual void on_end_transaction(EpochSys* esys, uint64_t c) = 0;
    virtual void sync(uint64_t c){}
    virtual ~EpochAdvancer(){}
};

class SingleThreadEpochAdvancer : public EpochAdvancer{
    // uint64_t trans_cnt;
    padded<uint64_t>* trans_cnts;
    uint64_t epoch_threshold = 0x1ULL << 19;
    uint64_t help_threshold = 0x1ULL << 6;
public:
    SingleThreadEpochAdvancer(GlobalTestConfig* gtc);
    void set_epoch_freq(int epoch_power);
    void set_help_freq(int help_power);
    void on_end_transaction(EpochSys* esys, uint64_t c);
    void sync(uint64_t c){
        errexit("SingleThreadEpochAdvancer::sync() not implemented.");
    }
};

class GlobalCounterEpochAdvancer : public EpochAdvancer{
    std::atomic<uint64_t> trans_cnt;
    uint64_t epoch_threshold = 0x1ULL << 14;
    uint64_t help_threshold = 0x1ULL;
public:
    // GlobalCounterEpochAdvancer();
    void set_epoch_freq(int epoch_power);
    void set_help_freq(int help_power);
    void on_end_transaction(EpochSys* esys, uint64_t c);
    void sync(uint64_t c){
        errexit("GlobalCounterEpochAdvancer::sync() not implemented.");
    }
};

class DedicatedEpochAdvancer : public EpochAdvancer{
    struct LocalSyncSignal{
        std::mutex bell;
        std::condition_variable ring;
        uint64_t curr_epoch;
    }__attribute__((aligned(CACHE_LINE_SIZE)));
    struct SyncQueueSignal{
        std::mutex bell;
        std::condition_variable ring;
        atomic<int> request_cnt;
        SyncQueueSignal(){
            request_cnt.store(0);
        }
    };
    EpochSys* esys;
    std::thread advancer_thread;
    std::atomic<bool> started;
    uint64_t epoch_length = 100*1000;
    std::vector<LocalSyncSignal> local_sync_singals;
    SyncQueueSignal sync_queue_signal;
    void advancer(int task_num);
public:
    DedicatedEpochAdvancer(GlobalTestConfig* gtc, EpochSys* es);
    ~DedicatedEpochAdvancer();
    void set_epoch_freq(int epoch_interval){}
    void set_help_freq(int help_interval){}
    void on_end_transaction(EpochSys* esys, uint64_t c){
        // do nothing here.
    }
    void sync(uint64_t c);
};

class NoEpochAdvancer : public EpochAdvancer{
    // an epoch advancer that does absolutely nothing.
public:
    // GlobalCounterEpochAdvancer();
    void set_epoch_freq(int epoch_power){}
    void set_help_freq(int help_power){}
    void on_end_transaction(EpochSys* esys, uint64_t c){}
    void sync(uint64_t c){}
};

}

#endif
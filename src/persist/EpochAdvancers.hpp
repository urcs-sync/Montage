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

class DedicatedEpochAdvancer : public EpochAdvancer{
    struct SyncSignal{
        std::mutex bell;
        std::condition_variable advancer_ring;
        std::condition_variable worker_ring;
        uint64_t target_epoch = INIT_EPOCH + 1;
    };
    GlobalTestConfig* gtc;
    EpochSys* esys;
    std::thread advancer_thread;
    std::atomic<bool> started;
    uint64_t epoch_length;
    SyncSignal sync_signal;
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
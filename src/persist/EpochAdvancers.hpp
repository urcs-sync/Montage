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
    virtual uint64_t ongoing_target() = 0; // for helper persisters (worker thraeds) only.
    virtual void set_epoch_freq(int epoch_freq) = 0;
    virtual void set_help_freq(int help_freq) = 0;
    virtual void on_end_transaction(EpochSys* esys, uint64_t c) = 0;
    virtual void sync(uint64_t c){}
    virtual ~EpochAdvancer(){}
};

class DedicatedEpochAdvancer : public EpochAdvancer{
    enum AdvancerState{
        INIT = 0,
        RUNNING = 1,
        ENDED = 2
    };
    GlobalTestConfig* gtc;
    EpochSys* esys;
    std::thread advancer_thread;
    std::atomic<AdvancerState> advancer_state;
    uint64_t epoch_length;
    hwloc_obj_t advancer_affinity = nullptr;
    paddedAtomic<uint64_t> target_epoch; // for helping from worker threads.
    void find_first_socket();
    void advancer(int task_num);
public:
    DedicatedEpochAdvancer(GlobalTestConfig* gtc, EpochSys* es);
    ~DedicatedEpochAdvancer();
    uint64_t ongoing_target();
    void set_epoch_freq(int epoch_interval){}
    void set_help_freq(int help_interval){}
    void on_end_transaction(EpochSys* esys, uint64_t c){
        // do nothing here.
    }
    void sync(uint64_t c);
};


class DedicatedEpochAdvancerNbSync : public EpochAdvancer{
    GlobalTestConfig* gtc;
    EpochSys* esys;
    std::thread advancer_thread;
    std::atomic<bool> started;
    uint64_t epoch_length;
    hwloc_obj_t advancer_affinity = nullptr;
    paddedAtomic<uint64_t> target_epoch; // for helping from worker threads.
    void find_first_socket();
    void advancer(int task_num);
public:
    DedicatedEpochAdvancerNbSync(GlobalTestConfig* gtc, EpochSys* es);
    ~DedicatedEpochAdvancerNbSync();
    uint64_t ongoing_target();
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
    uint64_t ongoing_target() {return INIT_EPOCH;}
    void set_epoch_freq(int epoch_power) {}
    void set_help_freq(int help_power) {}
    void on_end_transaction(EpochSys* esys, uint64_t c) {}
    void sync(uint64_t c) {}
};

}

#endif
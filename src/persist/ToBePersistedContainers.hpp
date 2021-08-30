#ifndef TO_BE_PERSISTED_CONTAINERS_HPP
#define TO_BE_PERSISTED_CONTAINERS_HPP

#include <condition_variable>
#include <thread>
#include <hwloc.h>
#include <atomic>

#include "TestConfig.hpp"
#include "ConcurrentPrimitives.hpp"
#include "PersistFunc.hpp"
#include "PerThreadContainers.hpp"
#include "persist_utils.hpp"
#include "common_macros.hpp"
#include "Persistent.hpp"

namespace pds{

class PBlk;
class EpochSys;

// template<typename A, typename B>
// struct PairHash{
//     size_t operator () (const pds::pair<A,B> &x) const{
//         return std::hash<A>{}(x.first);
//     }
// };

//////////////////////////////
// To-be-persist Containers //
//////////////////////////////

class ToBePersistContainer{
public:
    Ralloc* ral = nullptr;
    int task_num = -1;
    padded<void*>* descs_p = nullptr;
    paddedAtomic<bool>* desc_persist_indicators[EPOCH_WINDOW];
    virtual void init_desc_local(void* addr, int tid);
    virtual void register_persist_desc_local(uint64_t c, int tid);
    virtual void do_persist_desc_local(uint64_t c, int tid);
    virtual void register_persist(PBlk* blk, uint64_t c) = 0;
    virtual void register_persist_raw(PBlk* blk, uint64_t c) = 0;
    virtual void persist_epoch(uint64_t c) = 0;
    virtual void persist_epoch_local(uint64_t c, int tid) = 0;
    virtual void help_persist_external(uint64_t c) {}
    virtual void clear() = 0;
    ToBePersistContainer(Ralloc* r, int tn): ral(r), task_num(tn){
        descs_p = new padded<void*>[task_num];
        for (int i = 0; i < EPOCH_WINDOW; i++){
            desc_persist_indicators[i] = new paddedAtomic<bool>[task_num];
        }
        for (int i = 0; i < task_num; i++){
            descs_p[i].ui = nullptr;
            for (int j = 0; j < EPOCH_WINDOW; j++){
                desc_persist_indicators[j][i].ui = false;
            }
        }
    }
    ToBePersistContainer(){}
    virtual ~ToBePersistContainer() {
        if (task_num > 0){
            delete descs_p;
            for (int i = 0; i < EPOCH_WINDOW; i++){
                delete desc_persist_indicators[i];
            }
        }
    }
};

class DirWB : public ToBePersistContainer{
public:
    DirWB(Ralloc* r, int task_num) : ToBePersistContainer(r, task_num){}
    void register_persist_desc_local(uint64_t c, int tid) {
        void* blk = descs_p[tid].ui;
        persist_func::clwb_range_nofence(
            blk, ral->malloc_size(blk));
    }
    void register_persist(PBlk* blk, uint64_t c){
        assert(blk!=nullptr);
        persist_func::clwb_range_nofence(blk, ral->malloc_size(blk));
    }
    void register_persist_raw(PBlk* blk, uint64_t c){
        persist_func::clwb(blk);
    }
    void persist_epoch(uint64_t c){}
    void persist_epoch_local(uint64_t c, int tid){}
    void clear(){}
};

class BufferedWB : public ToBePersistContainer{
    // class Persister{
    // public:
    //     BufferedWB* con = nullptr;
    //     Persister(BufferedWB* _con) : con(_con){}
    //     virtual ~Persister() {}
    //     virtual void help_persist_local(uint64_t c) = 0;
    // };

    // class PerThreadDedicatedWait : public Persister{
    //     struct Signal {
    //         std::mutex bell;
    //         std::condition_variable ring;
    //         int curr = 0;
    //         uint64_t epoch = INIT_EPOCH;
    //     }__attribute__((aligned(CACHE_LINE_SIZE)));

    //     GlobalTestConfig* gtc;
    //     std::vector<std::thread> persisters;
    //     std::vector<hwloc_obj_t> persister_affinities;
    //     std::atomic<bool> exit;
    //     Signal* signals;
    //     // TODO: explain in comment what's going on here.
    //     void persister_main(int worker_id);
    // public:
    //     PerThreadDedicatedWait(BufferedWB* _con, GlobalTestConfig* _gtc);
    //     ~PerThreadDedicatedWait();
    //     void help_persist_local(uint64_t c);
    // };

    // class PerThreadDedicatedBusy : public Persister{
    //     struct Signal {
    //         std::atomic<int> curr;
    //         std::atomic<int> ack;
    //         uint64_t epoch = INIT_EPOCH;
    //     }__attribute__((aligned(CACHE_LINE_SIZE)));

    //     GlobalTestConfig* gtc;
    //     std::vector<std::thread> persisters;
    //     std::vector<hwloc_obj_t> persister_affinities;
    //     std::atomic<bool> exit;
    //     Signal* signals;
    //     // TODO: explain in comment what's going on here.
    //     void persister_main(int worker_id);
    // public:
    //     PerThreadDedicatedBusy(BufferedWB* _con, GlobalTestConfig* _gtc);
    //     ~PerThreadDedicatedBusy();
    //     void help_persist_local(uint64_t c);
    // };

    // class WorkerThreadPersister : public Persister{
    // public:
    //     WorkerThreadPersister(BufferedWB* _con) : Persister(_con) {}
    //     void help_persist_local(uint64_t c);
    // };

    // FixedCircBufferContainer<pds::pair>* container = nullptr;
    FixedContainer<void*>* container = nullptr;
    GlobalTestConfig* gtc;
    // Persister* persister = nullptr;
    int buffer_size = 64;
    void do_persist(void*& addr);
    // void dump(uint64_t c);
public:
    BufferedWB (GlobalTestConfig* _gtc, Ralloc* r): 
        ToBePersistContainer(r, _gtc->task_num), gtc(_gtc){
        if (gtc->checkEnv("BufferSize")){
            buffer_size = stoi(gtc->getEnv("BufferSize"));
        } else {
            buffer_size = 64;
        }
        // persister = new WorkerThreadPersister(this);
        if (gtc->checkEnv("Container")){
            std::string env_container = gtc->getEnv("Container");
            if (env_container == "CircBuffer"){
                container = new FixedCircBufferContainer<void*>(task_num, buffer_size);
            } else if (env_container == "HashSet"){
                container = new FixedHashSetContainer(task_num, buffer_size);
            } else {
                errexit("unsupported container type by BufferedWB");
            }
        } else {
            container = new FixedCircBufferContainer<void*>(task_num, buffer_size);
        }
        
        // if (gtc->checkEnv("Persister")){
        //     std::string env_persister = gtc->getEnv("Persister");
        //     if (env_persister == "PerThreadBusy"){
        //         persister = new PerThreadDedicatedBusy(this, gtc);
        //     } else if (env_persister == "PerThreadWait"){
        //         persister = new PerThreadDedicatedWait(this, gtc); 
        //     } else 
        //     if (env_persister == "Worker"){
        //         persister = new WorkerThreadPersister(this);
        //     } else {
        //         errexit("unsupported persister type by BufferedWB");
        //     }
        // } else {
        //     persister = new WorkerThreadPersister(this);
        // }
        
    }
    ~BufferedWB(){
        delete container;
        // delete persister;
    }
    inline void* mark_raw(void* ptr) {return (void*)((uint64_t)ptr | 0x1ULL);}
    inline bool is_raw(void* ptr) {return (((uint64_t)ptr & 0x1ULL) == 0x1ULL);}
    inline void* unmark_raw(void* ptr) {return (void*)((uint64_t)ptr & ~0x1ULL);}
    void register_persist(PBlk* blk, uint64_t c);
    void register_persist_raw(PBlk* blk, uint64_t c);
    void persist_epoch(uint64_t c);
    void persist_epoch_local(uint64_t c, int tid);
    void clear();
};

class NoToBePersistContainer : public ToBePersistContainer{
    // a to-be-persist container that does absolutely nothing.
    void init_desc_local(void* addr, int tid){}
    void register_persist_desc_local(uint64_t c, int tid){}
    void do_persist_desc_local(uint64_t c, int tid){}
    void register_persist(PBlk* blk, uint64_t c){}
    void register_persist_raw(PBlk* blk, uint64_t c){}
    void persist_epoch(uint64_t c){}
    void persist_epoch_local(uint64_t c, int tid){}
    void clear(){}
};

}

#endif
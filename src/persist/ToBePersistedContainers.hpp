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
    virtual void register_persist(PBlk* blk, size_t sz, uint64_t c) = 0;
    virtual void register_persist_raw(PBlk* blk, uint64_t c){
        persist_func::clwb(blk);
    }
    virtual void persist_epoch(uint64_t c) = 0;
    virtual void help_persist_external(uint64_t c) {}
    virtual void clear() = 0;
    virtual ~ToBePersistContainer() {}
};

class PerEpoch : public ToBePersistContainer{
    class Persister{
    public:
        PerEpoch* con = nullptr;
        Persister(PerEpoch* _con) : con(_con){}
        virtual ~Persister() {}
        virtual void persist_epoch(uint64_t c) = 0;
    };

    class AdvancerPersister : public Persister{
    public:
        AdvancerPersister(PerEpoch* _con): Persister(_con){}
        void persist_epoch(uint64_t c);
    };

    class PerThreadDedicatedWait : public Persister{
        struct Signal {
            std::mutex bell;
            std::condition_variable ring;
            uint64_t epoch = INIT_EPOCH;
            std::atomic<int> finish_counter;
        }__attribute__((aligned(CACHE_LINE_SIZE)));
        
        GlobalTestConfig* gtc;
        std::vector<std::thread> persisters;
        std::vector<hwloc_obj_t> persister_affinities;
        uint64_t last_persisted = NULL_EPOCH;
        
        std::atomic<bool> exit;
        Signal signal;
        // TODO: explain in comment what's going on here.
        void persister_main(int worker_id);
    public:
        PerThreadDedicatedWait(PerEpoch* _con, GlobalTestConfig* _gtc);
        ~PerThreadDedicatedWait();
        void persist_epoch(uint64_t c);
    };

    PerThreadContainer<pds::pair<void*, size_t>>* container = nullptr;
    Persister* persister = nullptr;
    static void do_persist(pds::pair<void*, size_t>& addr_size);
public:
    PerEpoch(GlobalTestConfig* gtc){
        if (gtc->checkEnv("Container")){
            std::string env_container = gtc->getEnv("Container");
            if (env_container == "CircBuffer"){
                container = new CircBufferContainer<pds::pair<void*, size_t>>(gtc->task_num);
            } else if (env_container == "Vector"){
                container = new VectorContainer<pds::pair<void*, size_t>>(gtc->task_num);
            }
            // else if (env_container == "HashSet"){
            //     container = new HashSetContainer<pds::pair<void*, size_t>, PairHash<const void*, size_t>>(gtc->task_num);
            // }
            else {
                errexit("unsupported container type by PerEpoch to-be-freed container.");
            }
        } else {
            container = new VectorContainer<pds::pair<void*, size_t>>(gtc->task_num);
        }

        if (gtc->checkEnv("Persister")){
            std::string env_persister = gtc->getEnv("Persister");
            if (env_persister == "PerThreadWait"){
                persister = new PerThreadDedicatedWait(this, gtc);
            } else if (env_persister == "Advancer"){
                persister = new AdvancerPersister(this);
            } else {
                errexit("unsupported persister type by PerEpoch to-be-freed container.");
            }
        } else {
            persister = new AdvancerPersister(this);
        }
    }
    ~PerEpoch(){
        delete persister;
        delete container;
    }
    void register_persist(PBlk* blk, size_t sz, uint64_t c);
    void register_persist_raw(PBlk* blk, uint64_t c);
    void persist_epoch(uint64_t c);
    void clear();
};

class DirWB : public ToBePersistContainer{
public:
    void register_persist(PBlk* blk, size_t sz, uint64_t c){
        persist_func::clwb_range_nofence(blk, sz);
    }
    void persist_epoch(uint64_t c){}
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

    // FixedCircBufferContainer<pds::pair<void*, size_t>>* container = nullptr;
    FixedContainer<pds::pair<void*, size_t>>* container = nullptr;
    GlobalTestConfig* gtc;
    // Persister* persister = nullptr;
    int task_num;
    int buffer_size = 2048;
    static void do_persist(pds::pair<void*, size_t>& addr_size);
    // void dump(uint64_t c);
public:
    BufferedWB (GlobalTestConfig* _gtc): gtc(_gtc), task_num(_gtc->task_num){
        if (gtc->checkEnv("BufferSize")){
            buffer_size = stoi(gtc->getEnv("BufferSize"));
        } else {
            buffer_size = 2048;
        }
        // persister = new WorkerThreadPersister(this);
        if (gtc->checkEnv("Container")){
            std::string env_container = gtc->getEnv("Container");
            if (env_container == "CircBuffer"){
                container = new FixedCircBufferContainer<pds::pair<void*, size_t>>(task_num, buffer_size);
            } else if (env_container == "HashSet"){
                container = new FixedHashSetContainer(task_num, buffer_size);
            } else {
                errexit("unsupported container type by BufferedWB");
            }
        } else {
            container = new FixedCircBufferContainer<pds::pair<void*, size_t>>(task_num, buffer_size);
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
    void push(pds::pair<void*, size_t> entry, uint64_t c);
    void register_persist(PBlk* blk, size_t sz, uint64_t c);
    void register_persist_raw(PBlk* blk, uint64_t c);
    void persist_epoch(uint64_t c);
    void clear();
};

class NoToBePersistContainer : public ToBePersistContainer{
    // a to-be-persist container that does absolutely nothing.
    void register_persist(PBlk* blk, size_t sz, uint64_t c){}
    void register_persist_raw(PBlk* blk, uint64_t c){}
    void persist_epoch(uint64_t c){}
    void clear(){}
};

}

#endif
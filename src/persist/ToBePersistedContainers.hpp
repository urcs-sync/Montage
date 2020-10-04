#ifndef TO_BE_PERSISTED_CONTAINERS_HPP
#define TO_BE_PERSISTED_CONTAINERS_HPP

template<typename A, typename B>
struct PairHash{
    size_t operator () (const std::pair<A,B> &x) const{
        return std::hash<A>{}(x.first);
    }
};

//////////////////////////////
// To-be-persist Containers //
//////////////////////////////

class ToBePersistContainer{
public:
    virtual void register_persist(PBlk* blk, uint64_t c) = 0;
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
        void persist_epoch(uint64_t c){
            con->container->pop_all(&do_persist, c);
        }
    };

    class PerThreadDedicatedWait : public Persister{
        struct Signal {
            std::mutex bell;
            std::condition_variable ring;
            int curr = 0;
            uint64_t epoch = INIT_EPOCH;
        }__attribute__((aligned(CACHELINE_SIZE)));

        GlobalTestConfig* gtc;
        std::vector<std::thread> persisters;
        std::vector<hwloc_obj_t> persister_affinities;
        atomic<bool> exit;
        Signal signal;
        // TODO: explain in comment what's going on here.
        void persister_main(int worker_id){
            // pin this thread to hyperthreads of worker threads.
            hwloc_set_cpubind(gtc->topology, 
                persister_affinities[worker_id]->cpuset,HWLOC_CPUBIND_THREAD);
            // spin until signaled to destruct.
            int last_signal = 0;
            int curr_signal = 0;
            int curr_epoch = NULL_EPOCH;
            while(!exit){
                // wait on worker (tid == worker_id) thread's signal.
                std::unique_lock<std::mutex> lck(signal.bell);
                while(last_signal == curr_signal && !exit){
                    curr_signal = signal.curr;
                    signal.ring.wait(lck);
                    curr_epoch = signal.epoch;
                }
                last_signal = curr_signal;
                // dumps
                con->container->pop_all_local(&do_persist, worker_id, curr_epoch);
            }
        }
    public:
        PerThreadDedicatedWait(PerEpoch* _con, GlobalTestConfig* _gtc) : Persister(_con), gtc(_gtc) {
            // re-build worker thread affinity that pin current threads to individual cores
            gtc->affinities.clear();
            gtc->buildPerCoreAffinity(gtc->affinities, 0);
            // build affinities that pin persisters to hyperthreads of worker threads
            gtc->buildPerCoreAffinity(persister_affinities, 1);
            // init environment
            exit.store(false, std::memory_order_relaxed);
            // spawn threads
            for (int i = 0; i < gtc->task_num; i++){
                persisters.push_back(std::move(
                    std::thread(&PerThreadDedicatedWait::persister_main, this, i)));
            }
        }
        ~PerThreadDedicatedWait(){
            // signal exit of worker threads.
            exit.store(true, std::memory_order_release);
            {
                std::unique_lock<std::mutex> lck(signal.bell);
                signal.curr++;
            }
            signal.ring.notify_all();
            // join threads
            for (auto i = persisters.begin(); i != persisters.end(); i++){
                if (i->joinable()){
                    i->join();
                }
            }
        }
        void persist_epoch(uint64_t c){
            // notify hyperthreads.
            {
                std::unique_lock<std::mutex> lck(signal.bell);
                signal.curr++;
                signal.epoch = c;
            }
            signal.ring.notify_all();
        }
    };

    PerThreadContainer<std::pair<void*, size_t>>* container = nullptr;
    Persister* persister = nullptr;
    static void do_persist(std::pair<void*, size_t>& addr_size){
        persist_func::clwb_range_nofence(
            addr_size.first, addr_size.second);
    }
public:
    PerEpoch(GlobalTestConfig* gtc){
        if (gtc->checkEnv("Container")){
            std::string env_container = gtc->getEnv("Container");
            if (env_container == "CircBuffer"){
                container = new CircBufferContainer<std::pair<void*, size_t>>(gtc->task_num);
            } else if (env_container == "Vector"){
                container = new VectorContainer<std::pair<void*, size_t>>(gtc->task_num);
            } else if (env_container == "HashSet"){
                container = new HashSetContainer<std::pair<void*, size_t>, PairHash<const void*, size_t>>(gtc->task_num);
            } else {
                errexit("unsupported container type by PerEpoch to-be-freed container.");
            }
        } else {
            container = new VectorContainer<std::pair<void*, size_t>>(gtc->task_num);
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
    void register_persist(PBlk* blk, uint64_t c){
        if (c == NULL_EPOCH){
            errexit("registering persist of epoch NULL.");
        }
        size_t sz = RP_malloc_size(blk);
        container->push(std::make_pair<void*, size_t>((char*)blk, (size_t)sz), c);
    }
    void register_persist_raw(PBlk* blk, uint64_t c){
        container->push(std::make_pair<void*, size_t>((char*)blk, 1), c);
    }
    void persist_epoch(uint64_t c){
        persister->persist_epoch(c);
    }
    void clear(){
        container->clear();
    }
};

class DirWB : public ToBePersistContainer{
public:
    void register_persist(PBlk* blk, uint64_t c){
        persist_func::clwb_range_nofence(blk, RP_malloc_size(blk));
    }
    void persist_epoch(uint64_t c){}
    void clear(){}
};

class BufferedWB : public ToBePersistContainer{
    class Persister{
    public:
        BufferedWB* con = nullptr;
        Persister(BufferedWB* _con) : con(_con){}
        virtual ~Persister() {}
        virtual void help_persist_local(uint64_t c) = 0;
    };

    class PerThreadDedicatedWait : public Persister{
        struct Signal {
            std::mutex bell;
            std::condition_variable ring;
            int curr = 0;
            uint64_t epoch = INIT_EPOCH;
        }__attribute__((aligned(CACHELINE_SIZE)));

        GlobalTestConfig* gtc;
        std::vector<std::thread> persisters;
        std::vector<hwloc_obj_t> persister_affinities;
        atomic<bool> exit;
        Signal* signals;
        // TODO: explain in comment what's going on here.
        void persister_main(int worker_id){
            // pin this thread to hyperthreads of worker threads.
            hwloc_set_cpubind(gtc->topology, 
                persister_affinities[worker_id]->cpuset,HWLOC_CPUBIND_THREAD);
            // spin until signaled to destruct.
            int last_signal = 0;
            int curr_signal = 0;
            uint64_t curr_epoch = NULL_EPOCH;

            while(!exit){
                // wait on worker (tid == worker_id) thread's signal.
                std::unique_lock<std::mutex> lck(signals[worker_id].bell);
                while(last_signal == curr_signal && !exit){
                    curr_signal = signals[worker_id].curr;
                    signals[worker_id].ring.wait(lck);
                    curr_epoch = signals[worker_id].epoch;
                }
                last_signal = curr_signal;
                // dumps
                for (int i = 0; i < con->dump_size; i++){
                    con->container->try_pop_local(&do_persist, worker_id, curr_epoch);
                }
            }
        }
    public:
        PerThreadDedicatedWait(BufferedWB* _con, GlobalTestConfig* _gtc) : Persister(_con), gtc(_gtc) {
            // re-build worker thread affinity that pin current threads to individual cores
            gtc->affinities.clear();
            gtc->buildPerCoreAffinity(gtc->affinities, 0);
            // build affinities that pin persisters to hyperthreads of worker threads
            gtc->buildPerCoreAffinity(persister_affinities, 1);
            // init environment
            exit.store(false, std::memory_order_relaxed);
            signals = new Signal[gtc->task_num];
            // spawn threads
            for (int i = 0; i < gtc->task_num; i++){
                persisters.push_back(std::move(
                    std::thread(&PerThreadDedicatedWait::persister_main, this, i)));
            }
        }
        ~PerThreadDedicatedWait(){
            // signal exit of worker threads.
            exit.store(true, std::memory_order_release);
            for (int i = 0; i < gtc->task_num; i++){
                // TODO: lock here?
                signals[i].curr++;
                signals[i].ring.notify_one();
            }
            // join threads
            for (auto i = persisters.begin(); i != persisters.end(); i++){
                if (i->joinable()){
                    // std::cout<<"joining thread."<<std::endl;
                    i->join();
                    // std::cout<<"joined."<<std::endl;
                }
            }
            delete signals;
        }
        void help_persist_local(uint64_t c){
            // notify hyperthread.
            {
                std::unique_lock<std::mutex> lck(signals[_tid].bell);
                signals[_tid].curr++;
                signals[_tid].epoch = c;
            }
            signals[_tid].ring.notify_one();
        }
    };

    class PerThreadDedicatedBusy : public Persister{
        struct Signal {
            std::atomic<int> curr;
            std::atomic<int> ack;
            uint64_t epoch = INIT_EPOCH;
        }__attribute__((aligned(CACHELINE_SIZE)));

        GlobalTestConfig* gtc;
        std::vector<std::thread> persisters;
        std::vector<hwloc_obj_t> persister_affinities;
        atomic<bool> exit;
        Signal* signals;
        // TODO: explain in comment what's going on here.
        void persister_main(int worker_id){
            // pin this thread to hyperthreads of worker threads.
            hwloc_set_cpubind(gtc->topology, 
                persister_affinities[worker_id]->cpuset,HWLOC_CPUBIND_THREAD);
            // spin until signaled to destruct.
            int last_signal = 0;
            int curr_signal = 0;
            uint64_t curr_epoch = NULL_EPOCH;
            while(!exit){
                // wait on worker (tid == worker_id) thread's signal.
                while(true){
                    if (exit.load(std::memory_order_acquire)){
                        return;
                    }
                    curr_signal = signals[worker_id].curr.load(std::memory_order_acquire);
                    if (curr_signal != last_signal){
                        break;
                    }
                }
                curr_epoch = signals[worker_id].epoch;
                signals[worker_id].ack.fetch_add(1, std::memory_order_release);
                last_signal = curr_signal;
                // dumps
                for (int i = 0; i < con->dump_size; i++){
                    con->container->try_pop_local(&do_persist, worker_id, curr_epoch);
                }
            }
        }
    public:
        PerThreadDedicatedBusy(BufferedWB* _con, GlobalTestConfig* _gtc) : Persister(_con), gtc(_gtc) {
            // re-build worker thread affinity that pin current threads to individual cores
            gtc->affinities.clear();
            gtc->buildPerCoreAffinity(gtc->affinities, 0);
            // build affinities that pin persisters to hyperthreads of worker threads
            gtc->buildPerCoreAffinity(persister_affinities, 1);
            // init environment
            exit.store(false, std::memory_order_relaxed);
            signals = new Signal[gtc->task_num];
            // spawn threads
            for (int i = 0; i < gtc->task_num; i++){
                signals[i].curr.store(0, std::memory_order_relaxed);
                signals[i].ack.store(0, std::memory_order_relaxed);
                persisters.push_back(std::move(
                    std::thread(&PerThreadDedicatedBusy::persister_main, this, i)));
            }
        }
        ~PerThreadDedicatedBusy(){
            // signal exit of worker threads.
            exit.store(true, std::memory_order_release);
            // join threads
            for (auto i = persisters.begin(); i != persisters.end(); i++){
                if (i->joinable()){
                    i->join();
                }
            }
            delete signals;
        }
        void help_persist_local(uint64_t c){
            // notify hyperthread.
            signals[_tid].epoch = c;
            int prev = signals[_tid].curr.fetch_add(1, std::memory_order_release);
            // make sure the persister gets the correct epoch.
            while(prev == signals[_tid].ack.load(std::memory_order_acquire));
        }
    };

    class WorkerThreadPersister : public Persister{
    public:
        WorkerThreadPersister(BufferedWB* _con) : Persister(_con) {}
        void help_persist_local(uint64_t c){
            for (int i = 0; i < con->dump_size; i++){
                con->container->try_pop_local(&do_persist, c);
            }
        }
    };

    FixedCircBufferContainer<std::pair<void*, size_t>>* container = nullptr;
    GlobalTestConfig* gtc;
    Persister* persister = nullptr;
    padded<int>* counters = nullptr;
    padded<std::mutex>* locks = nullptr;
    int task_num;
    int buffer_size = 2048;
    int dump_size = 1024;
    static void do_persist(std::pair<void*, size_t>& addr_size){
        persist_func::clwb_range_nofence(
            addr_size.first, addr_size.second);
    }
    void dump(uint64_t c){
        for (int i = 0; i < dump_size; i++){
            container->try_pop_local(&do_persist, c);
        }
    }
public:
    BufferedWB (GlobalTestConfig* _gtc): gtc(_gtc), task_num(_gtc->task_num){
        if (gtc->checkEnv("BufferSize")){
            buffer_size = stoi(gtc->getEnv("BufferSize"));
        } else {
            buffer_size = 2048;
        }
        dump_size = buffer_size / 2;
        if (gtc->checkEnv("DumpSize")){
            dump_size = stoi(gtc->getEnv("DumpSize"));
        }
        assert(buffer_size >= dump_size);
        assert(buffer_size > 1 && dump_size > 1);
        // persister = new WorkerThreadPersister(this);
        container = new FixedCircBufferContainer<std::pair<void*, size_t>>(task_num, buffer_size);
        if (gtc->checkEnv("Persister")){
            std::string env_persister = gtc->getEnv("Persister");
            if (env_persister == "PerThreadBusy"){
                persister = new PerThreadDedicatedBusy(this, gtc);
            } else if (env_persister == "PerThreadWait"){
                persister = new PerThreadDedicatedWait(this, gtc); 
            } else if (env_persister == "Worker"){
                persister = new WorkerThreadPersister(this);
            } else {
                errexit("unsupported persister type by BufferedWB");
            }
        } else {
            persister = new WorkerThreadPersister(this);
        }
        
    }
    ~BufferedWB(){
        delete container;
        delete counters;
        delete persister;
    }
    void push(std::pair<void*, size_t> entry, uint64_t c){
        while (!container->try_push(entry, c)){// in case other thread(s) are doing write-backs.
            persister->help_persist_local(c);
        }
    }
    void register_persist(PBlk* blk, uint64_t c){
        if (c == NULL_EPOCH){
            errexit("registering persist of epoch NULL.");
        }
        size_t sz = RP_malloc_size(blk);
        push(std::make_pair<void*, size_t>((char*)blk, (size_t)sz), c);
        
    }
    void register_persist_raw(PBlk* blk, uint64_t c){
        if (c == NULL_EPOCH){
            errexit("registering persist of epoch NULL.");
        }
        push(std::make_pair<void*, size_t>((char*)blk, 1), c);
    }
    void persist_epoch(uint64_t c){ // NOTE: this is not thread-safe.
        for (int i = 0; i < task_num; i++){
            container->pop_all_local(&do_persist, i, c);
        }
    }
    void clear(){
        container->clear();
    }
};

class NoToBePersistContainer : public ToBePersistContainer{
    // a to-be-persist container that does absolutely nothing.
    void register_persist(PBlk* blk, uint64_t c){}
    void register_persist_raw(PBlk* blk, uint64_t c){}
    void persist_epoch(uint64_t c){}
    void clear(){}
};

#endif
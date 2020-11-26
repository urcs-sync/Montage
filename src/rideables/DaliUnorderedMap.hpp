#ifndef DALI_UNORDEREDMAP
#define DALI_UNORDEREDMAP

#include <atomic>
#include <mutex>

#include "HarnessUtils.hpp"
#include "TestConfig.hpp"
#include <map>
#include <vector>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <sys/resource.h>
#include <hwloc.h>

#include "ConcurrentPrimitives.hpp"
// #include "HazardTracker.hpp"
#include "RMap.hpp"
#include <functional>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <unordered_set>
#include <thread>
#include <sys/time.h>
#include <PersistFunc.hpp>
#include "ralloc.hpp"

using namespace persist_func;
template <class K, class V, size_t idxSize=1000000>
class DaliUnorderedMap : public RMap<K,V>{
    struct Node{
        K key;
        optional<V> val;
        pptr<Node> next;
        Node(K k, optional<V> v, Node* n):key(k),val(v),next(n){};
        void* operator new(size_t size){
            // cout<<"persistent allocator called."<<endl;
            // void* ret = malloc(size);
            void* ret = RP_malloc(size);
            if (!ret){
                cerr << "Persistent::new failed: no free memory" << endl;
                exit(1);
            }
            return ret;
        }

        void operator delete(void * p) { 
            RP_free(p); 
        } 
    };

    struct Bucket{
        std::mutex lock;
        int64_t stat;// a,f,c,ss. 2/2/2/58 bits
        pptr<Node> ptrs[3];
        uint64_t gc_timer = 0;
        Bucket(){
            ptrs[0]=nullptr;
            ptrs[1]=nullptr;
            ptrs[2]=nullptr;
        };
        int64_t inline a(){return DaliUnorderedMap::a(stat);}
        int64_t inline f(){return DaliUnorderedMap::f(stat);}
        int64_t inline c(){return DaliUnorderedMap::c(stat);}
        int64_t inline ss(){return DaliUnorderedMap::ss(stat);}
    }__attribute__((aligned(CACHELINE_SIZE)));

    template <class T>
    class FList{
        struct FNode{
            const T val;
            FNode* next;
            FNode(T v,FNode* n):val(v),next(n){};
            FNode(T v):val(v),next(nullptr){};
            void* operator new(size_t size){
                // cout<<"persistent allocator called."<<endl;
                // void* ret = malloc(size);
                void* ret = RP_malloc(size);
                if (!ret){
                    cerr << "Persistent::new failed: no free memory" << endl;
                    exit(1);
                }
                return ret;
            }

            void operator delete(void * p) { 
                RP_free(p); 
            } 
        };
        FNode* head;
    public:
        void inline emplace(T v){//only called by a single worker thread
            FNode* old_head=head;
            head=new FNode(v,old_head);
        }
        bool inline find(T v){//true if v in the list; else false
            FNode* cur_head=head;
            if(cur_head == nullptr || cur_head->val < v) return false;
            for(;cur_head!=nullptr&&v<=cur_head->val;cur_head=cur_head->next){
                if(cur_head->val==v) return true;
            }
            return false;
        }
        FList():head(nullptr){};
        ~FList(){
            FNode* cur_head=head;
            while(cur_head!=nullptr){
                FNode* old_head=cur_head;
                cur_head=cur_head->next;
                delete old_head;
            }
        }
        void* operator new(size_t size){
            // cout<<"persistent allocator called."<<endl;
            // void* ret = malloc(size);
            void* ret = RP_malloc(size);
            if (!ret){
                cerr << "Persistent::new failed: no free memory" << endl;
                exit(1);
            }
            return ret;
        }

        void operator delete(void * p) { 
            RP_free(p); 
        } 
    };

    class RetiredContainer{
    private:
        std::vector<Node*> retired[4];
    public:
        void insert(Node* node, int64_t e){
            retired[e%4].push_back(node);
        }
        void reclaim(int64_t e){
            if (e < 0){
                return;
            }
            while(!retired[e%4].empty()){
                delete retired[e%4].back();
                retired[e%4].pop_back();
            }
        }
        ~RetiredContainer(){
            for (int i = 0; i < 4; i++){
                reclaim(i);
            }
        }
    }__attribute__((aligned(CACHELINE_SIZE)));

    enum FenceType{
        ft_WCRB, ft_sfence
    };

private:
    const int task_num;
    FenceType fence_type;

    Bucket* buckets;
    FList<int64_t>* flist;
    std::atomic<int64_t>* epoch;//the most significant bit is for epoch_not_flushed flag.

    std::hash<K> hash_fn;
    // std::atomic<uint64_t> op_cnt;
    padded<uint64_t>* op_cnts;
    uint64_t threshold = 1<<19;
    
    // paddedAtomic<size_t>* bucket_cnt = new paddedAtomic<size_t>[idxSize]{};
    int cleanInterval=50;//cleanup per <cleanInterval> times for one bucket
    paddedAtomic<int64_t>* reservation;// reserved epoch for each threads
    const int64_t EPOCH_MAX = 0x7fffffffffffffff;
    const int64_t EPOCH_NOT_FLUSHED = 0x8000000000000000;
    paddedAtomic<bool>* fence_requires = nullptr;

    // transient structures for GC:
    // per-thread per-epoch containers of retired Nodes.
    RetiredContainer* retired = nullptr;
    // per-thread timer: do actual reclamation every reclaim_threshold gc's.
    padded<uint64_t>* reclaim_timers = nullptr;
    // per-bucket timer threshold: do gc for the bucket every gc_threshold update's.
    uint64_t gc_threshold;
    uint64_t reclaim_threshold;

    // void cleanup(int idx, int tid);
    void advance_epoch(int tid);
    void require_fence(int tid);
    void check_fence_require(int tid);
    int64_t get_epoch(int tid);
    void end_op(int tid);
    Node* update(Bucket* cur_bucket, K key, optional<V> val, int tid);//helper func for update operations.
    int64_t lookup(int64_t e, bool cur_fail, bool pre_fail, Bucket* cur_bucket);//create a new stat according to the lookup table in Dali paper
    optional<V> find_value(Node* valid_head, K key);
    void do_gc(int64_t e, Bucket* cur_bucket, Node* valid_head, int tid);

    static inline int64_t a(int64_t stat){return (stat&0xc000000000000000)>>62;}
    static inline int64_t f(int64_t stat){return (stat&0x3000000000000000)>>60;}
    static inline int64_t c(int64_t stat){return (stat&0x0c00000000000000)>>58;}
    static inline int64_t ss(int64_t stat){return (stat&0x03ffffffffffffff);}
    static inline int64_t new_stat(int64_t a, int64_t f, int64_t c, int64_t ss){
        return ((a&0x3)<<62)|((f&0x3)<<60)|((c&0x3)<<58)|(ss&0x03ffffffffffffff);
    }

    FenceType get_fence_type(GlobalTestConfig* gtc){
        std::string type = gtc->getEnv("fence");
        FenceType ret;
        if (type == "WCRB"){
            ret = ft_WCRB;
        } else if (type == "sfence"){
            ret = ft_sfence;
        } else {
            if(gtc->verbose){
                std::cout<<"defalut fence: sfence"<<std::endl;
            }
            ret = ft_sfence;
        }
        return ret;
    }
public:
    DaliUnorderedMap(GlobalTestConfig* gtc):
            task_num(gtc->task_num){
        Persistent::init();
        buckets = (Bucket*)RP_malloc(sizeof(Bucket)*idxSize);
        new (buckets) Bucket [idxSize] ();
        flist = new FList<int64_t>();
        epoch = (std::atomic<int64_t>*)RP_malloc(sizeof(std::atomic<int64_t>));
        new (epoch) std::atomic<int64_t>();
        reservation = new paddedAtomic<int64_t>[task_num];
        for(int i=0;i<task_num;++i){
            reservation[i].ui.store(EPOCH_MAX,std::memory_order_release);
        }
        // for(int i=0;i<idxSize;++i){
        //     bucket_cnt[i].ui.store(0,std::memory_order_release);
        // }
        fence_type = get_fence_type(gtc);
        fence_requires = new paddedAtomic<bool>[gtc->task_num];
        for (int i = 0; i < gtc->task_num; i++){
            fence_requires[i].ui.store(false);
        }
        op_cnts = new padded<uint64_t>[gtc->task_num];
        for (int i = 0; i < gtc->task_num; i++){
            op_cnts[i].ui = 0;
        }

        if (gtc->checkEnv("EpochFreq")){
            int env_epoch_advance = stoi(gtc->getEnv("EpochFreq"));
            if (env_epoch_advance > 63){
                errexit("invalid EpochFreq power");
            }
            threshold = 0x1ULL << env_epoch_advance;
        }

        retired = new RetiredContainer[gtc->task_num];
        reclaim_timers = new padded<uint64_t>[gtc->task_num];
        reclaim_threshold = 0x1ULL << 8; // TDOO: tune this.
        gc_threshold = 0x1ULL << 6; // TDOO: tune this.
    };
    ~DaliUnorderedMap(){
        std::cout<<"current epoch: "<<epoch->load()<<std::endl;
        delete retired;
        Persistent::finalize();
    };

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Persistent::init_thread(gtc, ltc);
    }

    optional<V> get(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> remove(K key, int tid);
    optional<V> replace(K key, V val, int tid);
};

template <class T>
class DaliUnorderedMapFactory : public RideableFactory{
    DaliUnorderedMap<T,T>* build(GlobalTestConfig* gtc){
        return new DaliUnorderedMap<T,T>(gtc);
    }
};

//-------Definition----------
template <class K, class V,size_t idxSize>
void DaliUnorderedMap<K,V,idxSize>::advance_epoch(int tid){
    //write_lock if repurposing threads to be a worker.
    int64_t e = epoch->load(std::memory_order_acquire);
    e++;
    e = e | EPOCH_NOT_FLUSHED;

    epoch->store(e,std::memory_order_release);
    clwb(epoch);
    e = e & EPOCH_MAX;//unflag EPOCH_NOT_FLUSHED
    epoch->store(e,std::memory_order_release);

    int i=0;
    while(i<task_num){
        int64_t r=reservation[i].ui.load(std::memory_order_acquire);
        if(r>=e) i++;//thread i has left the epoch
    }
    sfence();
    // require_fence(tid);
    //wholewb();//TODO: Not really sure how to write back whole cache
    // std::cout<<"cur e: "<<e<<std::endl;//debug output
    //write_unlock if repurposing threads to be a worker.

    if (fence_type == ft_WCRB){
        epoch->fetch_add(1,std::memory_order_relaxed);
        // clwb(epoch);//TODO: not really sure clwb an atomic obj.
        // sfence();//TODO: Not really sure how to wait for all threads
        wholewb();//TODO: Not really sure how to write back whole cache
    }
}

template <class K, class V,size_t idxSize>
void DaliUnorderedMap<K,V,idxSize>::require_fence(int tid){
    sfence();
    for (int i = 0; i < task_num; i++){
        fence_requires[i].ui.store(true);
    }
    fence_requires[tid].ui.store(false);
    bool completed = false;
    while(!completed){
        completed = true;
        for (int i = 0; i < task_num; i++){
            if (fence_requires[i].ui.load() == true){
                completed = false;
            }
        }
    }
}

template <class K, class V,size_t idxSize>
void DaliUnorderedMap<K,V,idxSize>::check_fence_require(int tid){
    // clear fence request
    if (fence_requires[tid].ui.load() == true){
        sfence();
        fence_requires[tid].ui.store(false);
    }
}

template <class K, class V,size_t idxSize>
int64_t DaliUnorderedMap<K,V,idxSize>::get_epoch(int tid){
    while(true){
        int64_t e = epoch->load(std::memory_order_acquire);
        if(e & EPOCH_NOT_FLUSHED){
            clwb(epoch);
            e = e & EPOCH_MAX;
        }
        reservation[tid].ui.store(e,std::memory_order_release);
        mfence();//ensure epoch->load happens after reservation
        if((epoch->load(std::memory_order_acquire)&EPOCH_MAX) == e)
            return e;
    }
}

template <class K, class V,size_t idxSize>
void DaliUnorderedMap<K,V,idxSize>::end_op(int tid){
    // TODO: should we remove per-thread fence, given that store implies mfence?
    // check_fence_require(tid);
    reservation[tid].ui.store(EPOCH_MAX,std::memory_order_release);

    op_cnts[tid].ui++;
    if(tid == 0 && op_cnts[tid].ui % threshold == 0){
        advance_epoch(tid);
    }
}

template <class K, class V,size_t idxSize>
typename DaliUnorderedMap<K,V,idxSize>::Node* DaliUnorderedMap<K,V,idxSize>::update(Bucket* cur_bucket, K key, optional<V> val, int tid) {
    Node* valid_head=nullptr;
    bool cur_fail=flist->find(cur_bucket->ss());
    bool pre_fail=flist->find(cur_bucket->ss()-1) || cur_bucket->ptrs[cur_bucket->f()]==nullptr;
    if(!cur_fail)
        valid_head=cur_bucket->ptrs[cur_bucket->a()];
    else if(!pre_fail) 
        valid_head=cur_bucket->ptrs[cur_bucket->f()];
    else
        valid_head=cur_bucket->ptrs[cur_bucket->c()];
    Node* n=new Node(key, val, valid_head);
    int64_t new_stat=lookup(get_epoch(tid),
        cur_fail,pre_fail,cur_bucket);
    cur_bucket->ptrs[a(new_stat)]=n;
    cur_bucket->stat=new_stat;
    
    if (++(cur_bucket->gc_timer) >= gc_threshold && valid_head){
        do_gc(epoch->load(std::memory_order_acquire), cur_bucket, valid_head, tid);
    }
    // flush the cl of current node, and the bucket.
    if (fence_type == ft_sfence){
        clwb(cur_bucket);
        clwb(n);
    }
    return valid_head;
}

template <class K, class V,size_t idxSize>
int64_t DaliUnorderedMap<K,V,idxSize>::lookup(int64_t e, bool cur_fail, bool pre_fail, Bucket* cur_bucket){
    int64_t stat = cur_bucket->stat;
    int64_t ret = 0;
    if(ss(stat)==e) return stat;
    else if(ss(stat)==(e-1)){
        if(!cur_fail){
            if(!pre_fail) ret = new_stat(c(stat),a(stat),f(stat),e);
            else ret = new_stat(f(stat),a(stat),c(stat),e);
        }
        else{
            ret = new_stat(a(stat),f(stat),c(stat),e);
            cur_bucket->ptrs[f(ret)]=nullptr;//set f to invalid
        }
    }
    else{//ss<e-1
        if(!cur_fail){
            ret = new_stat(c(stat),f(stat),a(stat),e);
            cur_bucket->ptrs[f(ret)]=nullptr;//set f to invalid
        }
        else{
            if(!pre_fail){
                ret = new_stat(a(stat),c(stat),f(stat),e);
                cur_bucket->ptrs[f(ret)]=nullptr;//set f to invalid
            }
            else{
                ret = new_stat(a(stat),f(stat),c(stat),e);
                cur_bucket->ptrs[f(ret)]=nullptr;//set f to invalid
            }
        }
    }
    return ret;
}

template <class K, class V,size_t idxSize>
inline optional<V> DaliUnorderedMap<K,V,idxSize>::find_value(Node* valid_head, K key){
    optional<V> res={};
    while(valid_head!=nullptr){
        if((K)valid_head->key == key) {
            res = valid_head->val;
            break;
        }
        valid_head=valid_head->next;
    }
    return res;
}

template <class K, class V,size_t idxSize>
void DaliUnorderedMap<K,V,idxSize>::do_gc(int64_t e, Bucket* curr_bucket, Node* valid_head, int tid){
    assert(valid_head);
    // clear curr_bucket's gc timer
    curr_bucket->gc_timer = 0;
    // reclaim epochs older than e-1 if this thread's reclamation timer is up.
    if (++reclaim_timers[tid].ui >= reclaim_threshold){
        reclaim_timers[tid].ui = 0;
        // I think we don't need to worry about epoch delays.
        retired[tid].reclaim(e-2);
        retired[tid].reclaim(e-3);
    }
    // create a temporary set/map ts for keys seen during traversal
    std::unordered_set<K> seen_keys;
    // traverse the bucket
    Node* prev = valid_head;
    Node* curr = valid_head->next;
    seen_keys.insert(prev->key);
    while(curr){
        // if a key is not in ts, add it
        if (seen_keys.find(curr->key) == seen_keys.end()){
            seen_keys.insert(curr->key);
        } else {
            // otherwise, the key will be removed. unlink it from the bucket.
            prev->next = curr->next;
            // put it in its corresponding retire list.
            retired[tid].insert(curr, e);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
}

template <class K, class V,size_t idxSize>
optional<V> DaliUnorderedMap<K,V,idxSize>::get(K key, int tid) {
    optional<V> res={};
    size_t idx=hash_fn(key)%idxSize;
    Node* valid_head=nullptr;

    buckets[idx].lock.lock();
    Bucket* cur_bucket=&(buckets[idx]);
    if(!flist->find(cur_bucket->ss()))
        valid_head=cur_bucket->ptrs[cur_bucket->a()];
    else if(!flist->find(cur_bucket->ss()-1) && cur_bucket->ptrs[cur_bucket->f()]!=nullptr)
        valid_head=cur_bucket->ptrs[cur_bucket->f()];
    else
        valid_head=cur_bucket->ptrs[cur_bucket->c()];
    res=find_value(valid_head,key);
    buckets[idx].lock.unlock();

    return res;
}

template <class K, class V,size_t idxSize>
optional<V> DaliUnorderedMap<K,V,idxSize>::put(K key, V val, int tid) {
    optional<V> res={};
    size_t idx=hash_fn(key)%idxSize;
    Node* valid_head=nullptr;//the old head pointer

    buckets[idx].lock.lock();
    Bucket* cur_bucket=&(buckets[idx]);
    valid_head=update(cur_bucket,key,val,tid);
    res=find_value(valid_head,key);
    buckets[idx].lock.unlock();
    end_op(tid);

    return res;
}
template <class K, class V,size_t idxSize>
bool DaliUnorderedMap<K,V,idxSize>::insert(K key, V val, int tid){
    bool res=false;
    size_t idx=hash_fn(key)%idxSize;
    Node* valid_head=nullptr;//the old head pointer

    buckets[idx].lock.lock();
    Bucket* cur_bucket=&(buckets[idx]);
    if(!flist->find(cur_bucket->ss()))
        valid_head=cur_bucket->ptrs[cur_bucket->a()];
    else if(!flist->find(cur_bucket->ss()-1) && cur_bucket->ptrs[cur_bucket->f()]!=nullptr)
        valid_head=cur_bucket->ptrs[cur_bucket->f()];
    else
        valid_head=cur_bucket->ptrs[cur_bucket->c()];
    if(find_value(valid_head,key).has_value()) res=false;
    else{
        update(cur_bucket,key,val,tid);
        res=true;
    }
    buckets[idx].lock.unlock();
    end_op(tid);
    // if(res)
    // 	std::cout<<"[I]key: "<<key<<" val: "<<val<<std::endl;
    // else
    // 	std::cout<<"[I]"<<key<<" fails!"<<std::endl;
    return res;
}

template <class K, class V,size_t idxSize>
optional<V> DaliUnorderedMap<K,V,idxSize>::remove(K key, int tid) {
    optional<V> res={};
    size_t idx=hash_fn(key)%idxSize;
    Node* valid_head=nullptr;//the old head pointer

    buckets[idx].lock.lock();
    Bucket* cur_bucket=&(buckets[idx]);
    if(!flist->find(cur_bucket->ss()))
        valid_head=cur_bucket->ptrs[cur_bucket->a()];
    else if(!flist->find(cur_bucket->ss()-1) && cur_bucket->ptrs[cur_bucket->f()]!=nullptr)
        valid_head=cur_bucket->ptrs[cur_bucket->f()];
    else
        valid_head=cur_bucket->ptrs[cur_bucket->c()];
    res=find_value(valid_head,key);
    if(res.has_value()){
        optional<V> val={};
        update(cur_bucket,key,val,tid);
    }
    buckets[idx].lock.unlock();
    end_op(tid);
    // if(res.has_value())
    // 	std::cout<<"[rm]key: "<<key<<" val: "<<res.value()<<std::endl;
    // else
    // 	std::cout<<"[rm]key: "<<key<<" val: "<<std::endl;

    return res;
}

template <class K, class V,size_t idxSize>
optional<V> DaliUnorderedMap<K,V,idxSize>::replace(K key, V val, int tid) {
    optional<V> res={};
    size_t idx=hash_fn(key)%idxSize;
    Node* valid_head=nullptr;//the old head pointer

    buckets[idx].lock.lock();
    Bucket* cur_bucket=&(buckets[idx]);
    if(!flist->find(cur_bucket->ss()))
        valid_head=cur_bucket->ptrs[cur_bucket->a()];
    else if(!flist->find(cur_bucket->ss()-1) && cur_bucket->ptrs[cur_bucket->f()]!=nullptr)
        valid_head=cur_bucket->ptrs[cur_bucket->f()];
    else
        valid_head=cur_bucket->ptrs[cur_bucket->c()];
    res=find_value(valid_head,key);
    if(res.has_value())
        update(cur_bucket,key,val,tid);
    buckets[idx].lock.unlock();
    end_op(tid);

    return res;
}


/* for string */
#include "PString.hpp"
template <>
struct DaliUnorderedMap<std::string,std::string,1000000>::Node{
    // TODO: This should be pptr<char> rather than basic_string
    pds::TrivialPString<TESTS_KEY_SIZE> key;
    pds::TrivialPString<TESTS_VAL_SIZE> val;
    pptr<Node> next;
    Node(std::string k, optional<std::string> v, Node* n):key(k),val(v.has_value()?v.value():""),next(n){
        key.flush();
        val.flush();
    };
    void* operator new(size_t size){
        // cout<<"persistent allocator called."<<endl;
        // void* ret = malloc(size);
        void* ret = RP_malloc(size);
        if (!ret){
            cerr << "Persistent::new failed: no free memory" << endl;
            exit(1);
        }
        return ret;
    }

    void operator delete(void * p) { 
        RP_free(p); 
    } 
};


#endif

#ifndef MONTAGE_LF_HASHTABLE_P
#define MONTAGE_LF_HASHTABLE_P

#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

#include <iostream>
#include <atomic>
#include <algorithm>
#include <functional>
#include <vector>
#include <utility>

#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RMap.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"
#include "Recoverable.hpp"

template <class K, class V, int idxSize=1000000>
class MontageLfHashTable : public RMap<K,V>, public Recoverable{
public:
    class Payload : public pds::PBlk{
        GENERATE_FIELD(K, key, Payload);
        GENERATE_FIELD(V, val, Payload);
    public:
        Payload(){}
        Payload(K x, V y): m_key(x), m_val(y){}
        Payload(const Payload& oth): pds::PBlk(oth), m_key(oth.m_key), m_val(oth.m_val){}
        void persist(){}
    }__attribute__((aligned(CACHELINE_SIZE)));
private:
    struct Node;

    struct MarkPtr{
        pds::atomic_lin_var<Node*> ptr;
        MarkPtr(Node* n):ptr(n){};
        MarkPtr():ptr(nullptr){};
    };

    struct Node{
        MontageLfHashTable* ds;
        MarkPtr next;
        Payload* payload;// TODO: does it have to be atomic?
        K key;
        Node(MontageLfHashTable* ds_, K k, V v, Node* n):
            ds(ds_),next(n),key(k){
            payload = ds->pnew<Payload>(k,v);
            // assert(ds->epochs[pds::EpochSys::tid].ui == NULL_EPOCH);
            };
        Node(Payload* _payload) : payload(_payload),key(_payload->get_unsafe_key(ds)) {} // for recovery
        K get_key(){
            return key;
        }
        ~Node(){
            if(payload)
                ds->preclaim(payload);
        }

        void retire_payload(){
            // call it before END_OP but after linearization point
            assert(payload!=nullptr && "payload shouldn't be null");
            ds->pretire(payload);
        }
        V get_val(){
            // call it within BEGIN_OP and END_OP
            assert(payload!=nullptr && "payload shouldn't be null");
            return (V)payload->get_val(ds);
        }
        V get_unsafe_val(){
            return (V)payload->get_unsafe_val(ds);
        }
    }__attribute__((aligned(CACHELINE_SIZE)));
    std::hash<K> hash_fn;
    padded<MarkPtr>* buckets=new padded<MarkPtr>[idxSize]{};
    bool findNode(MarkPtr* &prev, Node* &curr, Node* &next, K key, int tid);

    RCUTracker tracker;
    GlobalTestConfig* gtc;

    const uint64_t MARK_MASK = ~0x1;
    inline Node* getPtr(Node* d){
        return reinterpret_cast<Node*>((uint64_t)d & MARK_MASK);
    }
    inline bool getMark(Node* d){
        return (bool)((uint64_t)d & 1);
    }
    inline Node* mixPtrMark(Node* d, bool mk){
        return reinterpret_cast<Node*>((uint64_t)d | mk);
    }
    inline Node* setMark(Node* d){
        return reinterpret_cast<Node*>((uint64_t)d | 1);
    }
public:
    MontageLfHashTable(GlobalTestConfig* gtc) : Recoverable(gtc), tracker(gtc->task_num, 100, 1000, true), gtc(gtc) {
        if (get_recovered_pblks()) {
            recover();
        }
    };
    ~MontageLfHashTable(){
        recover_mode(); // PDELETE --> noop
        // clear transient structures.
        clear();
        online_mode(); // re-enable PDELETE.
    };

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }
    void clear(){
        //single-threaded; for recovery test only
        for (uint64_t i = 0; i < idxSize; i++){
            Node* curr = buckets[i].ui.ptr.load(this);
            Node* next = nullptr;
            while(curr){
                next = curr->next.ptr.load(this);
                delete curr;
                curr = next;
            }
            buckets[i].ui.ptr.store(this,nullptr);
        }
    }
    int recover(){
        int rec_cnt = 0;
        int rec_thd = gtc->task_num;
        if (gtc->checkEnv("RecoverThread")){
            rec_thd = stoi(gtc->getEnv("RecoverThread"));
        }
        std::unordered_map<uint64_t, pds::PBlk*>* recovered = get_recovered_pblks(); 
        assert(recovered);

        std::vector<Payload*> payloadVector;
        payloadVector.reserve(recovered->size());
        auto begin = chrono::high_resolution_clock::now();
        for (auto itr = recovered->begin(); itr != recovered->end(); itr++){
            rec_cnt++;
            Payload* payload = reinterpret_cast<Payload*>(itr->second);
            payloadVector.push_back(payload);
        }
        auto end = chrono::high_resolution_clock::now();
        auto dur = end - begin;
        auto dur_ms_vec = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms_vec << "ms building vector" << std::endl;
        begin = chrono::high_resolution_clock::now();
        std::vector<std::thread> workers;
        for (int rec_tid = 0; rec_tid < rec_thd; rec_tid++) {
            workers.emplace_back(std::thread([&, rec_tid]() {
                Recoverable::init_thread(rec_tid);
                hwloc_set_cpubind(gtc->topology,
                                  gtc->affinities[rec_tid]->cpuset,
                                  HWLOC_CPUBIND_THREAD);
                for (size_t i = rec_tid; i < payloadVector.size(); i += rec_thd) {
                    // re-insert payload.
                    Node* tmpNode = new Node(payloadVector[i]);
                    K key = tmpNode->get_key();
                    size_t idx = hash_fn(key) % idxSize;
                    MarkPtr* prev = nullptr;
                    Node* curr;
                    Node* next;
                    while (true) {
                        if (findNode(prev, curr, next, key, rec_tid)) {
                            errexit("conflicting keys recovered.");
                        } else {
                            // does not exist, insert.
                            tmpNode->next.ptr.store(this, curr);
                            // begin_op();
                            if (prev->ptr.CAS(this,curr, tmpNode)) {
                                // end_op();
                                break;
                            }
                            // abort_op();
                        }
                    }
                }
            }));  // workers.emplace_back()
        }// for (rec_thd)
        for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
        }
        end = chrono::high_resolution_clock::now();
        dur = end - begin;
        auto dur_ms_ins = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms_ins << "ms inserting(" << recovered->size() << ")" << std::endl;
        delete recovered;
        return rec_cnt;
    }

    optional<V> get(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> remove(K key, int tid);
    optional<V> replace(K key, V val, int tid);
};

template <class T> 
class MontageLfHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MontageLfHashTable<T,T>(gtc);
    }
};


//-------Definition----------
template <class K, class V, int idxSize> 
optional<V> MontageLfHashTable<K,V,idxSize>::get(K key, int tid) {
    optional<V> res={};
    MarkPtr* prev=nullptr;
    Node* curr;
    Node* next;

    tracker.start_op(tid);
    // hold epoch from advancing so that the node we find won't be deleted
    if(findNode(prev,curr,next,key,tid)) {
        // MontageOpHolder _holder(this);
        res=curr->get_unsafe_val();//never old see new as we find node before BEGIN_OP
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V, int idxSize> 
optional<V> MontageLfHashTable<K,V,idxSize>::put(K key, V val, int tid) {
    optional<V> res={};
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    Node* curr;
    Node* next;
    tmpNode = new Node(this, key, val, nullptr);

    tracker.start_op(tid);
    while(true) {
        if(findNode(prev,curr,next,key,tid)) {
            // exists; replace
            tmpNode->next.ptr.store(this,next);
            // begin_op();
            res=curr->get_unsafe_val();
            curr->retire_payload();
            // insert tmpNode after cur and mark cur
            if(curr->next.ptr.CAS_verify(this,next,setMark(tmpNode))) {
                // end_op();
                if(prev->ptr.CAS(this,curr,tmpNode)) {
                    tracker.retire(curr,tid);
                } else {
                    findNode(prev,curr,next,key,tid);
                }
                break;
            }
            // abort_op();
        }
        else {
            //does not exist; insert.
            res={};
            tmpNode->next.ptr.store(this,curr);
            // begin_op();
            if(prev->ptr.CAS_verify(this,curr,tmpNode)) {
                // end_op();
                break;
            }
            // abort_op();
        }
    }
    tracker.end_op(tid);
    // assert(0&&"put isn't implemented");
    return res;
}

template <class K, class V, int idxSize> 
bool MontageLfHashTable<K,V,idxSize>::insert(K key, V val, int tid){
    bool res=false;
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    Node* curr;
    Node* next;
    tmpNode = new Node(this, key, val, nullptr);

    tracker.start_op(tid);
    while(true) {
        if(findNode(prev,curr,next,key,tid)) {
            res=false;
            delete tmpNode;
            break;
        }
        else {
            //does not exist, insert.
            tmpNode->next.ptr.store(this,curr);
            // begin_op();
            if(prev->ptr.CAS_verify(this,curr,tmpNode)) {
                // end_op();
                res=true;
                break;
            }
            // abort_op();
        }
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V, int idxSize> 
optional<V> MontageLfHashTable<K,V,idxSize>::remove(K key, int tid) {
    optional<V> res={};
    MarkPtr* prev=nullptr;
    Node* curr;
    Node* next;

    tracker.start_op(tid);
    while(true) {
        if(!findNode(prev,curr,next,key,tid)) {
            res={};
            break;
        }
        // begin_op();
        res=curr->get_unsafe_val();
        curr->retire_payload();
        if(!curr->next.ptr.CAS_verify(this,next,setMark(next))) {
            // abort_op();
            continue;
        }
        // end_op();
        if(prev->ptr.CAS(this,curr,next)) {
            tracker.retire(curr,tid);
        } else {
            findNode(prev,curr,next,key,tid);
        }
        break;
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V, int idxSize> 
optional<V> MontageLfHashTable<K,V,idxSize>::replace(K key, V val, int tid) {
    optional<V> res={};
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    Node* curr;
    Node* next;
    tmpNode = new Node(this, key, val, nullptr);

    tracker.start_op(tid);
    while(true){
        if(findNode(prev,curr,next,key,tid)){
            tmpNode->next.ptr.store(this,next);
            // begin_op();
            res=curr->get_unsafe_val();
            curr->retire_payload();
            // insert tmpNode after cur and mark cur
            if(curr->next.ptr.CAS_verify(this,next,setMark(tmpNode))) {
                // end_op();
                if(prev->ptr.CAS(this,curr,tmpNode)) {
                    tracker.retire(curr,tid);
                } else {
                    findNode(prev,curr,next,key,tid);
                }
                break;
            }
            // abort_op();
        }
        else{//does not exist
            res={};
            delete tmpNode;
            break;
        }
    }
    tracker.end_op(tid);
    return res;
}

template <class K, class V, int idxSize> 
bool MontageLfHashTable<K,V,idxSize>::findNode(MarkPtr* &prev, Node* &curr, Node* &next, K key, int tid){
    size_t idx=hash_fn(key)%idxSize;
    while(true){
        bool cmark=false;
        prev=&buckets[idx].ui;
        curr=getPtr(prev->ptr.load(this));

        while(true){
            if(curr==nullptr) return false;
            next=curr->next.ptr.load(this);
            cmark=getMark(next);
            next=getPtr(next);
            auto ckey=curr->get_key();
            if(prev->ptr.load(this)!=curr) break;//retry
            if(!cmark) {
                if(ckey>=key) return ckey==key;
                prev=&(curr->next);
            } else {
                if(prev->ptr.CAS(this,curr,next)) {
                    tracker.retire(curr,tid);
                } else {
                    break;//retry
                }
            }
            curr=next;
        }
    }
}

/* Specialization for strings */
#include <string>
#include "InPlaceString.hpp"
template <>
class MontageLfHashTable<std::string, std::string>::Payload : public pds::PBlk{
    GENERATE_FIELD(pds::InPlaceString<TESTS_KEY_SIZE>, key, Payload);
    GENERATE_FIELD(pds::InPlaceString<TESTS_VAL_SIZE>, val, Payload);

public:
    Payload(std::string k, std::string v) : m_key(this, k), m_val(this, v){}
    Payload(const Payload& oth) : pds::PBlk(oth), m_key(this, oth.m_key), m_val(this, oth.m_val){}
    void persist(){}
};

#endif

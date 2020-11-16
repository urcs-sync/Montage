#ifndef MONTAGE_LF_HASHTABLE_P
#define MONTAGE_LF_HASHTABLE_P

#include <stdio.h>
#include <stdlib.h>

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

template <class K, class V>
class MontageLfHashTable : public RMap<K,V>, Recoverable{
public:
    class Payload : public PBlk{
        GENERATE_FIELD(K, key, Payload);
        GENERATE_FIELD(V, val, Payload);
    public:
        Payload(){}
        Payload(K x, V y): m_key(x), m_val(y){}
        // Payload(const Payload& oth): PBlk(oth), m_key(oth.m_key), m_val(oth.m_val){}
        void persist(){}
    };
private:
    struct Node;

    struct MarkPtr{
        atomic_lin_var<Node*> ptr;
        MarkPtr(Node* n):ptr(n){};
        MarkPtr():ptr(nullptr){};
    };

    struct Node{
        MontageLfHashTable* ds;
        K key;
        MarkPtr next;
        Payload* payload;// TODO: does it have to be atomic?
        Node(MontageLfHashTable* ds_, K k, V v, Node* n):
            ds(ds_),key(k),next(n),payload(ds_->pnew<Payload>(k,v)){
            // assert(ds->epochs[pds::EpochSys::tid].ui == NULL_EPOCH);
            };
        ~Node(){
            ds->preclaim(payload);
        }

        void rm_payload(){
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
    };
    std::hash<K> hash_fn;
    const int idxSize=1000000;//number of buckets for hash table
    padded<MarkPtr>* buckets=new padded<MarkPtr>[idxSize]{};
    bool findNode(MarkPtr* &prev, lin_var &curr, lin_var &next, K key, int tid);

    RCUTracker<Node> tracker;

    const uint64_t MARK_MASK = ~0x1;
    inline lin_var getPtr(const lin_var& d){
        return lin_var(d.val & MARK_MASK, d.cnt);
    }
    inline bool getMark(const lin_var& d){
        return (bool)(d.val & 1);
    }
    inline lin_var mixPtrMark(const lin_var& d, bool mk){
        return lin_var(d.val | mk, d.cnt);
    }
    inline Node* setMark(const lin_var& d){
        return reinterpret_cast<Node*>(d.val | 1);
    }
public:
    MontageLfHashTable(GlobalTestConfig* gtc) : Recoverable(gtc), tracker(gtc->task_num, 100, 1000, true) {
    };
    ~MontageLfHashTable(){};

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    int recover(bool simulated){
        errexit("recover of MontageLfHashTable not implemented.");
        return 0;
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
template <class K, class V> 
optional<V> MontageLfHashTable<K,V>::get(K key, int tid) {
    optional<V> res={};
    MarkPtr* prev=nullptr;
    lin_var curr;
    lin_var next;

    tracker.start_op(tid);
    // hold epoch from advancing so that the node we find won't be deleted
    if(findNode(prev,curr,next,key,tid)) {
        MontageOpHolder(this);
        res=curr.get_val<Node*>()->get_unsafe_val();//never old see new as we find node before BEGIN_OP
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V> 
optional<V> MontageLfHashTable<K,V>::put(K key, V val, int tid) {
    optional<V> res={};
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    lin_var curr;
    lin_var next;
    tmpNode = new Node(this, key, val, nullptr);

    tracker.start_op(tid);
    while(true) {
        if(findNode(prev,curr,next,key,tid)) {
            // exists; replace
            tmpNode->next.ptr.store(curr);
            begin_op();
            res=curr.get_val<Node*>()->get_val();
            if(prev->ptr.CAS_verify(this,curr,tmpNode)) {
                curr.get_val<Node*>()->rm_payload();
                end_op();
                // mark curr; since findNode only finds the first node >= key, it's ok to have duplicated keys temporarily
                while(!curr.get_val<Node*>()->next.ptr.CAS(next,setMark(next)));
                if(tmpNode->next.ptr.CAS(curr,next)) {
                    tracker.retire(curr.get_val<Node*>(),tid);
                } else {
                    findNode(prev,curr,next,key,tid);
                }
                break;
            }
            abort_op();
        }
        else {
            //does not exist; insert.
            res={};
            tmpNode->next.ptr.store(curr);
            begin_op();
            if(prev->ptr.CAS_verify(this,curr,tmpNode)) {
                end_op();
                break;
            }
            abort_op();
        }
    }
    tracker.end_op(tid);
    // assert(0&&"put isn't implemented");
    return res;
}

template <class K, class V> 
bool MontageLfHashTable<K,V>::insert(K key, V val, int tid){
    bool res=false;
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    lin_var curr;
    lin_var next;
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
            tmpNode->next.ptr.store(curr);
            begin_op();
            if(prev->ptr.CAS_verify(this,curr,tmpNode)) {
                end_op();
                res=true;
                break;
            }
            abort_op();
        }
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V> 
optional<V> MontageLfHashTable<K,V>::remove(K key, int tid) {
    optional<V> res={};
    MarkPtr* prev=nullptr;
    lin_var curr;
    lin_var next;

    tracker.start_op(tid);
    while(true) {
        if(!findNode(prev,curr,next,key,tid)) {
            res={};
            break;
        }
        begin_op();
        res=curr.get_val<Node*>()->get_val();
        if(!curr.get_val<Node*>()->next.ptr.CAS_verify(this,next,setMark(next))) {
            abort_op();
            continue;
        }
        curr.get_val<Node*>()->rm_payload();
        end_op();
        if(prev->ptr.CAS(curr,next)) {
            tracker.retire(curr.get_val<Node*>(),tid);
        } else {
            findNode(prev,curr,next,key,tid);
        }
        break;
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V> 
optional<V> MontageLfHashTable<K,V>::replace(K key, V val, int tid) {
    optional<V> res={};
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    lin_var curr;
    lin_var next;
    tmpNode = new Node(this, key, val, nullptr);

    tracker.start_op(tid);
    while(true){
        if(findNode(prev,curr,next,key,tid)){
            tmpNode->next.ptr.store(curr);
            begin_op();
            res=curr.get_val<Node*>()->get_val();
            if(prev->ptr.CAS_verify(this,curr,tmpNode)){
                curr.get_val<Node*>()->rm_payload();
                end_op();
                // mark curr; since findNode only finds the first node >= key, it's ok to have duplicated keys temporarily
                while(!curr.get_val<Node*>()->next.ptr.CAS(next,setMark(next)));
                if(tmpNode->next.ptr.CAS(curr,next)) {
                    tracker.retire(curr.get_val<Node*>(),tid);
                } else {
                    findNode(prev,curr,next,key,tid);
                }
                break;
            }
            abort_op();
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

template <class K, class V> 
bool MontageLfHashTable<K,V>::findNode(MarkPtr* &prev, lin_var &curr, lin_var &next, K key, int tid){
    while(true){
        size_t idx=hash_fn(key)%idxSize;
        bool cmark=false;
        prev=&buckets[idx].ui;
        curr=getPtr(prev->ptr.load(this));

        while(true){//to lock old and curr
            if(curr.get_val<Node*>()==nullptr) return false;
            next=curr.get_val<Node*>()->next.ptr.load(this);
            cmark=getMark(next);
            next=getPtr(next);
            auto ckey=curr.get_val<Node*>()->key;
            if(prev->ptr.load(this)!=curr) break;//retry
            if(!cmark) {
                if(ckey>=key) return ckey==key;
                prev=&(curr.get_val<Node*>()->next);
            } else {
                if(prev->ptr.CAS(curr,next)) {
                    tracker.retire(curr.get_val<Node*>(),tid);
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
#include "PString.hpp"
template <>
class MontageLfHashTable<std::string, std::string>::Payload : public PBlk{
    GENERATE_FIELD(PString<TESTS_KEY_SIZE>, key, Payload);
    GENERATE_FIELD(PString<TESTS_VAL_SIZE>, val, Payload);

public:
    Payload(std::string k, std::string v) : m_key(this, k), m_val(this, v){}
    void persist(){}
};

#endif

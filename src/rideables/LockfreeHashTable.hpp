#ifndef LOCKFREE_HASHTABLE
#define LOCKFREE_HASHTABLE

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

template <class K, class V>
class LockfreeHashTable : public RMap<K,V>{
    struct Node;

    struct MarkPtr{
        std::atomic<Node*> ptr;
        MarkPtr(Node* n):ptr(n){};
        MarkPtr():ptr(nullptr){};
    };

    struct Node{
        K key;
        V val;
        MarkPtr next;
        Node(K k, V v, Node* n):key(k),val(v),next(n){};
    };
private:
    std::hash<K> hash_fn;
    const int idxSize=1000000;//number of buckets for hash table
    padded<MarkPtr>* buckets=new padded<MarkPtr>[idxSize]{};
    bool findNode(MarkPtr* &prev, Node* &curr, Node* &next, K key, int tid);

    RCUTracker tracker;

    const uint64_t MARK_MASK = ~0x1;
    inline Node* getPtr(Node* mptr){
        return (Node*) ((uint64_t)mptr & MARK_MASK);
    }
    inline bool getMark(Node* mptr){
        return (bool)((uint64_t)mptr & 1);
    }
    inline Node* mixPtrMark(Node* ptr, bool mk){
        return (Node*) ((uint64_t)ptr | mk);
    }
    inline Node* setMark(Node* mptr){
        return mixPtrMark(mptr,true);
    }
public:
    LockfreeHashTable(int task_num) : tracker(task_num, 100, 1000, true) {};
    ~LockfreeHashTable(){};

    optional<V> get(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> remove(K key, int tid);
    optional<V> replace(K key, V val, int tid);
};

template <class T> 
class LockfreeHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new LockfreeHashTable<T,T>(gtc->task_num);
    }
};


//-------Definition----------
template <class K, class V> 
optional<V> LockfreeHashTable<K,V>::get(K key, int tid) {
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    optional<V> res={};

    tracker.start_op(tid);
    if(findNode(prev,curr,next,key,tid)) {
        res=curr->val;
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V> 
optional<V> LockfreeHashTable<K,V>::put(K key, V val, int tid) {
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    optional<V> res={};
    tmpNode = new Node(key, val, nullptr);

    tracker.start_op(tid);
    while(true) {
        if(findNode(prev,curr,next,key,tid)) {
            // exists; replace
            res=curr->val;
            tmpNode->next.ptr.store(next);
            // insert tmpNode after cur and mark cur
            if(curr->next.ptr.compare_exchange_strong(next,setMark(tmpNode))){
                if(prev->ptr.compare_exchange_strong(curr,tmpNode)) {
                    tracker.retire(curr,tid);
                } else {
                    findNode(prev,curr,next,key,tid);
                }
                break;
            }
        }
        else {
            //does not exist; insert.
            res={};
            tmpNode->next.ptr.store(curr);
            if(prev->ptr.compare_exchange_strong(curr,tmpNode)) {
                break;
            }
        }
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V> 
bool LockfreeHashTable<K,V>::insert(K key, V val, int tid){
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    bool res=false;
    tmpNode = new Node(key, val, nullptr);

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
            if(prev->ptr.compare_exchange_strong(curr,tmpNode)) {
                res=true;
                break;
            }
        }
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V> 
optional<V> LockfreeHashTable<K,V>::remove(K key, int tid) {
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    optional<V> res={};

    tracker.start_op(tid);
    while(true) {
        if(!findNode(prev,curr,next,key,tid)) {
            res={};
            break;
        }
        res=curr->val;
        if(!curr->next.ptr.compare_exchange_strong(next,setMark(next))) {
            continue;
        }
        if(prev->ptr.compare_exchange_strong(curr,next)) {
            tracker.retire(curr,tid);
        } else {
            findNode(prev,curr,next,key,tid);
        }
        break;
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V> 
optional<V> LockfreeHashTable<K,V>::replace(K key, V val, int tid) {
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    optional<V> res={};
    tmpNode = new Node(key, val, nullptr);

    tracker.start_op(tid);
    while(true){
        if(findNode(prev,curr,next,key,tid)){
            res=curr->val;
            res=curr->val;
            tmpNode->next.ptr.store(next);
            // insert tmpNode after cur and mark cur
            if(curr->next.ptr.compare_exchange_strong(next,setMark(tmpNode))){
                if(prev->ptr.compare_exchange_strong(curr,tmpNode)) {
                    tracker.retire(curr,tid);
                } else {
                    findNode(prev,curr,next,key,tid);
                }
                break;
            }
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
bool LockfreeHashTable<K,V>::findNode(MarkPtr* &prev, Node* &curr, Node* &next, K key, int tid){
    while(true){
        size_t idx=hash_fn(key)%idxSize;
        bool cmark=false;
        prev=&buckets[idx].ui;
        curr=getPtr(prev->ptr.load());

        while(true){//to lock old and curr
            if(curr==nullptr) return false;
            next=curr->next.ptr.load();
            cmark=getMark(next);
            next=getPtr(next);
            auto ckey=curr->key;
            if(prev->ptr.load()!=curr) break;//retry
            if(!cmark) {
                if(ckey>=key) return ckey==key;
                prev=&(curr->next);
            } else {
                if(prev->ptr.compare_exchange_strong(curr,next)) {
                    tracker.retire(curr,tid);
                } else {
                    break;//retry
                }
            }
            curr=next;
        }
    }
}

#endif

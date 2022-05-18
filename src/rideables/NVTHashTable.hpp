#ifndef NVTRAVERSE_HASHTABLE_HPP
#define NVTRAVERSE_HASHTABLE_HPP

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
#include "PersistFunc.hpp"
#include "InPlaceString.hpp"

// NVTraverse-based nonblocking hashtable
// https://dl.acm.org/doi/pdf/10.1145/3385412.3386031?casa_token=wPPFarFf6NEAAAAA:iLbzTjoilFePXUceguEoYQxDBIYcJV1yYePfkzwCnO6dbeMqYZY76a9kXIhhyZUGzmzmlQ1fHBs8dSg

using namespace persist_func;
class NVTHashTable : public RMap<std::string,std::string>{
    template <class T>
    class my_alloc {
    public:
        typedef T value_type;
        typedef std::size_t size_type;
        typedef T& reference;
        typedef const T& const_reference;

        T* allocate(std::size_t n) {
            // assert(alloc != NULL);
            if (auto p = static_cast<T*>(RP_malloc(n * sizeof(T)))) return p;
            throw std::bad_alloc();
        }

        void deallocate(T *ptr, std::size_t) noexcept {
            // assert(alloc != NULL);
            RP_free(ptr);
        }
    };
    struct Node;

    struct MarkPtr{
        std::atomic<Node*> ptr;
        MarkPtr(Node* n):ptr(n){};
        MarkPtr():ptr(nullptr){};
    };

    struct Node : public Persistent{
        pds::InPlaceString<TESTS_KEY_SIZE> key;
        pds::InPlaceString<TESTS_VAL_SIZE> val;
        MarkPtr next;
        Node(std::string k, std::string v, Node* n):key(k),val(v),next(n){
            clwb_obj_nofence(this); // flush after write
        };
    };
private:
    std::hash<std::string> hash_fn;
    const int idxSize=1000000;//number of buckets for hash table
    padded<MarkPtr>* buckets=new padded<MarkPtr>[idxSize]{};
    bool findNode(MarkPtr* &prev, Node* &curr, Node* &next, std::string key, int tid);

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
    NVTHashTable(int task_num) : tracker(task_num, 100, 1000, true) {
        Persistent::init();
    };
    ~NVTHashTable(){
        Persistent::finalize();
    };

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Persistent::init_thread(gtc, ltc);
    }

    optional<std::string> get(std::string key, int tid);
    optional<std::string> put(std::string key, std::string val, int tid);
    bool insert(std::string key, std::string val, int tid);
    optional<std::string> remove(std::string key, int tid);
    optional<std::string> replace(std::string key, std::string val, int tid);
};

class NVTHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new NVTHashTable(gtc->task_num);
    }
};


//-------Definition----------
optional<std::string> NVTHashTable::get(std::string key, int tid) {
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    optional<std::string> res={};

    tracker.start_op(tid);
    if(findNode(prev,curr,next,key,tid)) {
        res=curr->val;
    }
    tracker.end_op(tid);
    sfence(); // fence before return
    return res;
}

optional<std::string> NVTHashTable::put(std::string key, std::string val, int tid) {
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    optional<std::string> res={};
    tmpNode = new Node(key, val, nullptr);

    tracker.start_op(tid);
    while(true) {
        if(findNode(prev,curr,next,key,tid)) {
            // exists; replace
            res=curr->val;
            tmpNode->next.ptr.store(next);
            clwb_obj_fence(tmpNode); // flush after write, fence before CAS.
            // insert tmpNode after cur and mark cur
            if(curr->next.ptr.compare_exchange_strong(next,setMark(tmpNode))){
                clwb_fence(&curr->next); // flush after CAS, fence before CAS.
                if(prev->ptr.compare_exchange_strong(curr,tmpNode)) {
                    clwb_obj_nofence(&prev->ptr); // flush after CAS
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
            clwb_obj_fence(&tmpNode->next); // flush after write, fence before CAS.
            if(prev->ptr.compare_exchange_strong(curr,tmpNode)) {
                clwb_obj_nofence(&prev->ptr); // flush after CAS
                break;
            }
        }
    }
    tracker.end_op(tid);
    sfence(); // fence before return
    return res;
}

bool NVTHashTable::insert(std::string key, std::string val, int tid){
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
            clwb_obj_fence(&tmpNode->next); // flush after write, fence before CAS.
            if(prev->ptr.compare_exchange_strong(curr,tmpNode)) {
                clwb_obj_fence(&prev->ptr); // flush after CAS
                res=true;
                break;
            }
        }
    }
    tracker.end_op(tid);
    sfence(); // fence before return
    return res;
}

optional<std::string> NVTHashTable::remove(std::string key, int tid) {
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    optional<std::string> res={};

    tracker.start_op(tid);
    while(true) {
        if(!findNode(prev,curr,next,key,tid)) {
            res={};
            break;
        }
        res=curr->val;
        sfence(); // fence before CAS
        if(!curr->next.ptr.compare_exchange_strong(next,setMark(next))) {
            clwb_obj_nofence(&curr->next); // flush after CAS
            continue;
        }
        clwb_obj_fence(&curr->next); // flush after CAS, fence before CAS
        if(prev->ptr.compare_exchange_strong(curr,next)) {
            clwb_obj_nofence(&prev->ptr); // flush after CAS
            tracker.retire(curr,tid);
        } else {
            findNode(prev,curr,next,key,tid);
        }
        break;
    }
    tracker.end_op(tid);
    sfence(); // fence before return
    return res;
}

optional<std::string> NVTHashTable::replace(std::string key, std::string val, int tid) {
    Node* tmpNode = nullptr;
    MarkPtr* prev=nullptr;
    Node* curr=nullptr;
    Node* next=nullptr;
    optional<std::string> res={};
    tmpNode = new Node(key, val, nullptr);

    tracker.start_op(tid);
    while(true){
        if(findNode(prev,curr,next,key,tid)){
            res=curr->val;
            tmpNode->next.ptr.store(next);
            clwb_obj_fence(tmpNode); // flush after write, fence before CAS.
            // insert tmpNode after cur and mark cur
            if(curr->next.ptr.compare_exchange_strong(next,setMark(tmpNode))){
                clwb_fence(&curr->next); // flush after CAS, fence before CAS.
                if(prev->ptr.compare_exchange_strong(curr,tmpNode)) {
                    clwb_obj_nofence(&prev->ptr); // flush after CAS
                    tracker.retire(curr,tid);
                } else {
                    findNode(prev,curr,next,key,tid);
                }
                clwb_obj_nofence(&tmpNode->next); // flush after CAS
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
    sfence(); // fence before return
    return res;
}

bool NVTHashTable::findNode(MarkPtr* &prev, Node* &curr, Node* &next, std::string key, int tid){
    // NOTE: all clwb instructions in findNode contributes to
    // "flush a path of length k back from n" requirement.
    while(true){
        size_t idx=hash_fn(key)%idxSize;
        bool cmark=false;
        prev=&buckets[idx].ui;
        curr=getPtr(prev->ptr.load());
        // clwb_obj_nofence(&prev->ptr);
        clwb_obj_nofence(prev);
        while(true){//to lock old and curr
            if(curr==nullptr) return false;
            next=curr->next.ptr.load();
            // clwb_obj_nofence(&curr->next);
            cmark=getMark(next);
            next=getPtr(next);
            int cmp = curr->key.compare(key);
            // clwb_obj_nofence(&curr->key);
            clwb_obj_nofence(curr);
            if(prev->ptr.load()!=curr) break;//retry
            // clwb_obj_nofence(&prev->ptr);
            clwb_obj_nofence(prev);
            if(!cmark) {
                if(cmp == 0) {
                    // sfence();
                    return true;
                } else if (cmp > 0) {
                    return false;
                }
                prev=&(curr->next);
            } else {
                // sfence();
                if(prev->ptr.compare_exchange_strong(curr,next)) {
                    clwb_obj_nofence(&prev->ptr); // flush after CAS
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

#ifndef SSHASHTABLE_HPP
#define SSHASHTABLE_HPP
#include <cstdio>
#include <cstdlib>

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
class SSHashTable : public RMap<K, V> {
    struct Node;
    struct MarkPtr {
        std::atomic<Node *> ptr;
        MarkPtr(Node *n) : ptr(n){};
        MarkPtr() : ptr(nullptr){};
    };

    struct Node {
        size_t so_k;
        K key;
        V val;
        MarkPtr next;
        Node(size_t so, K k, V v, Node *n) : so_k(so),key(k), val(v), next(n){};
        Node(size_t so) : so_k(so),key(), val(), next(nullptr){};
    };

private:
    std::atomic<int> size = (1<<20); //number of buckets for hash table
    const int MAX_LOAD = 3;
    const uint64_t MARK_MASK = ~0x1;
    padded<MarkPtr> *buckets;
    atomic<int32_t> count = {0};
    RCUTracker tracker;
    std::hash<K> myhash;

    padded<MarkPtr*> *prev;
    padded<Node*> *curr;
    padded<Node*> *next;


    inline Node *getPtr(Node *mptr) {
        return (Node *)((uint64_t)mptr & MARK_MASK);
    }
    inline bool getMark(Node *mptr) {
        return (bool)((uint64_t)mptr & 1);
    }
    inline Node *mixPtrMark(Node *ptr, bool mk) {
        return (Node *)((uint64_t)ptr | mk);
    }
    inline Node *setMark(Node *mptr) {
        return mixPtrMark(mptr, true);
    }

    void initialize_bucket(int bucket, int tid);
    inline int get_parent(int bucket){
        int msb = 1<< ((sizeof(int)*8)-__builtin_clz(bucket)-1);
        int result = bucket & ~msb;
        return result;
    }
    inline size_t reverse_bits(size_t n){
        size_t ans = 0;
        for (int i = sizeof(size_t) * 8 - 1; i >= 0; i--) {
            ans |= (n & 1) << i;
            n >>= 1;
        }
        return ans;
    }
    inline size_t so_regularkey(size_t key){
        return reverse_bits(key | (1ULL << (sizeof(size_t) * 8 - 1)));
    }
    inline size_t so_dummykey(size_t key){
        return reverse_bits(key);
    }
    bool list_insert(MarkPtr *head, Node *node, int tid);
    bool list_find(MarkPtr *head, size_t so_k, K key, int tid);
    optional<V> list_delete(MarkPtr *head, size_t so_k, K key, int tid);

public:
    SSHashTable(int task_num) : tracker(task_num, 100, 1000, true){
        buckets = new padded<MarkPtr>[size.load()] {};
        prev = new padded<MarkPtr*>[task_num];
        curr = new padded<Node*>[task_num];
        next = new padded<Node*>[task_num];
    };
    ~SSHashTable(){};

    optional<V> get(K key, int tid);
    optional<V> remove(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> replace(K key, V val, int tid);
};

template <class T> 
class SSHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new SSHashTable<T,T>(gtc->task_num);
    }
};

template <class K, class V>
void SSHashTable<K, V>::initialize_bucket(int bucket, int tid)
{
    int parent = get_parent(bucket);

    if (parent!=0 && buckets[parent].ui.ptr == nullptr) {
        initialize_bucket(parent,tid);
    }

    Node* dummy = new Node(so_dummykey(bucket));
    if (!list_insert(&(buckets[parent].ui), dummy, tid)) {
        delete dummy;
        dummy = curr[tid].ui;
    }

    Node* expected = nullptr;
    buckets[parent].ui.ptr.compare_exchange_strong(expected, dummy);
}

template <class K, class V>
bool SSHashTable<K, V>::insert(K key, V val, int tid)
{
    size_t hashed = myhash(key);
    bool res = false;
    Node* node = new Node(so_regularkey(hashed), key, val, nullptr);
    int bucket = hashed % size;

    tracker.start_op(tid);
    if (buckets[bucket].ui.ptr.load() == nullptr) {
        initialize_bucket(bucket,tid);
    }
    if (!list_insert(&(buckets[bucket].ui), node, tid)) {
        delete node;
    } else {
        res = true;
        int csize = size.load();
        if (count.fetch_add(1) / csize > MAX_LOAD) {
            size.compare_exchange_strong(csize,csize<<1);
        }
    }

    tracker.end_op(tid);

    return res;
}

template <class K, class V>
optional<V> SSHashTable<K, V>::get(K key, int tid)
{
    size_t hashed = myhash(key);
    optional<V> res = {};
    int bucket = hashed % size;

    tracker.start_op(tid);
    if (buckets[bucket].ui.ptr.load() == nullptr) {
        initialize_bucket(bucket,tid);
    }
    if(list_find(&(buckets[bucket].ui), so_regularkey(hashed), key, tid)){
        res = curr[tid].ui->val;
    }

    tracker.end_op(tid);

    return res;
}

template <class K, class V>
optional<V> SSHashTable<K, V>::remove(K key, int tid)
{
    size_t hashed = myhash(key);
    optional<V> res;
    int bucket = hashed % size;

    tracker.start_op(tid);
    if (buckets[bucket].ui.ptr.load() == nullptr) {
        initialize_bucket(bucket,tid);
    }
    if(res=list_delete(&(buckets[bucket].ui), so_regularkey(hashed), key, tid)){
        count.fetch_sub(1);
    }
    
    tracker.end_op(tid);

    return res;
}

template <class K, class V>
bool SSHashTable<K, V>::list_find(MarkPtr* head, size_t so_k, K key, int tid)
{
    while (true){
        bool cmark = false;
        prev[tid].ui = head;
        curr[tid].ui = getPtr(prev[tid].ui->ptr.load());
        while (true) {
            if (curr[tid].ui == nullptr)
                return false;
            next[tid].ui = curr[tid].ui->next.ptr.load();
            cmark = getMark(next[tid].ui);
            next[tid].ui = getPtr(next[tid].ui);
            auto ckey = curr[tid].ui->so_k;
            if (prev[tid].ui->ptr.load() != curr[tid].ui)
                break; //retry
            if (!cmark) {
                if (ckey > so_k) return false;
                if (ckey == so_k && curr[tid].ui->key==key) return true;
                prev[tid].ui = &(curr[tid].ui->next);
            } else {
                if (prev[tid].ui->ptr.compare_exchange_strong(curr[tid].ui, next[tid].ui)) {
                    tracker.retire(curr[tid].ui, tid);
                } else {
                    break; //retry
                }
            }
            curr[tid].ui = next[tid].ui;
        }
    }
}

template <class K, class V>
bool SSHashTable<K, V>::list_insert(MarkPtr *head, Node *node, int tid){
    bool res = false;
    K key = node->key;

    while (true) {
        if (list_find(head, node->so_k, key, tid)) {
            res = false;
            break;
        } else {
            //does not exist, insert.
            node->next.ptr.store(curr[tid].ui);
            if (prev[tid].ui->ptr.compare_exchange_strong(curr[tid].ui, node)) {
                res = true;
                break;
            }
        }
    }
    return res;
}

template <class K, class V>
optional<V> SSHashTable<K, V>::list_delete(MarkPtr *head, size_t so_k, K key, int tid)
{
    optional<V> res;
    while (true) {
        if (!list_find(head, so_k, key, tid)) {
            res={};
            break;
        }
        res = curr[tid].ui->val;
        if (!curr[tid].ui->next.ptr.compare_exchange_strong(next[tid].ui, setMark(next[tid].ui))) {
            continue;
        }
        if (prev[tid].ui->ptr.compare_exchange_strong(curr[tid].ui, next[tid].ui)) {
            tracker.retire(curr[tid].ui, tid);
        } else {
            list_find(head, so_k, key, tid);
        }
        break;
    }
    return res;
}

template <class K, class V>
optional<V> SSHashTable<K, V>::put(K key, V val, int tid)
{
    optional<V> res;
    assert(0&&"insert not implemented!");
    return res;
}

template <class K, class V>
optional<V> SSHashTable<K, V>::replace(K key, V val, int tid)
{
    optional<V> res;
    assert(0&&"replace not implemented!");
    return res;
}

#endif

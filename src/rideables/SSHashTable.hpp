#ifndef SSHASHTABLE_HPP
#define SSHASHTABLE_HPP
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
class SSHashTable : public RMap<K, V>
{
    struct Node;

    struct MarkPtr
    {
        std::atomic<Node *> ptr;
        MarkPtr(Node *n) : ptr(n){};
        MarkPtr() : ptr(nullptr){};
    };

    struct Node
    {
        K key;
        V val;
        MarkPtr next;
        Node(K k, V v, Node *n) : key(k), val(v), next(n){};
    };

private:
    std::atomic<int> size = 1000000; //number of buckets for hash table
    int MAX_LOAD = 100000000;
    padded<MarkPtr> *buckets;
    atomic<int32_t> count = {0};
    RCUTracker<Node> tracker;
    //TODO : must be thread private
    padded<MarkPtr*> *prev;
    padded<MarkPtr> *curr;
    padded<MarkPtr> *next;

    const uint64_t MARK_MASK = ~0x1;

    inline Node *getPtr(Node *mptr)
    {
        return (Node *)((uint64_t)mptr & MARK_MASK);
    }
    inline bool getMark(Node *mptr)
    {
        return (bool)((uint64_t)mptr & 1);
    }
    inline Node *mixPtrMark(Node *ptr, bool mk)
    {
        return (Node *)((uint64_t)ptr | mk);
    }
    inline Node *setMark(Node *mptr)
    {
        return mixPtrMark(mptr, true);
    }

public:
    SSHashTable(int task_num) : tracker(task_num, 100, 1000, true){
        buckets = new padded<MarkPtr>[size.load()] {};
        prev = new padded<MarkPtr*>[task_num];
        curr = new padded<MarkPtr>[task_num];
        next = new padded<MarkPtr>[task_num];
    };
    ~SSHashTable(){};

    void initialize_bucket(int bucket, int tid);
    int get_parent(int bucket);
    K reverse_bits(K n);
    K so_regularkey(K key);
    K so_dummykey(K key);
    optional<V> get(K key, int tid);
    optional<V> remove(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool list_insert(MarkPtr *head, Node *node, int tid);
    bool list_find(MarkPtr *head, K key, int tid);
    optional<V> list_delete(MarkPtr *head, K key, int tid);
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
int SSHashTable<K, V>::get_parent(int bucket)
{
     int msb = 1<< ((sizeof(int)*8)-__builtin_clz(bucket)-1);
     int result = bucket & ~msb;
     return result;
}

template <class K, class V>
K SSHashTable<K, V>::reverse_bits(K n)
{
    K ans = 0;
    for (int i = sizeof(K) * 8 - 1; i >= 0; i--)
    {
        ans |= (n & 1) << i;
        n >>= 1;
    }
    return ans;
}

template <class K, class V>
K SSHashTable<K, V>::so_regularkey(K key)
{
    return reverse_bits(key | (1ULL << (sizeof(K) * 8 - 1)));
}

template <class K, class V>
void SSHashTable<K, V>::initialize_bucket(int bucket, int tid)
{
    // tracker.start_op(tid);

    int parent = get_parent(bucket);

    if (buckets[parent].ui.ptr == nullptr)
    {
        initialize_bucket(parent,tid);
    }

    Node* dummy = new Node(so_dummykey(bucket), 0, nullptr);
    if (!list_insert(&(buckets[parent].ui), dummy, tid))
    {
        delete dummy;
        dummy = curr[tid].ui.ptr.load();
    }

    Node* expected = nullptr;
    buckets[parent].ui.ptr.compare_exchange_strong(expected, dummy);

    // tracker.end_op(tid);
}

template <class K, class V>
optional<V> SSHashTable<K, V>::put(K key, V val, int tid)
{
    tracker.start_op(tid);

    bool res = false;
    Node* node = new Node(so_regularkey(key), val, nullptr);
    int bucket = key % size;

    if (buckets[bucket].ui.ptr.load() == nullptr)
    {
        initialize_bucket(bucket,tid);
    }
    if (!list_insert(&(buckets[bucket].ui), node, tid))
    {

        tracker.retire(node, tid);
        delete node;
    } else {

        res = true;
        int csize = size.load();

        if (count.fetch_add(1) / csize > MAX_LOAD)
        {
            size.compare_exchange_strong(csize,2*csize);

        }

    }

    tracker.end_op(tid);

    return res;
}

template <class K, class V>
optional<V> SSHashTable<K, V>::get(K key, int tid)
{

    tracker.start_op(tid);

    optional<V> res = {};
    int bucket = key % size;

    if (buckets[bucket].ui.ptr.load() == nullptr)
    {
        initialize_bucket(bucket,tid);
    }
    res = list_find(&(buckets[bucket].ui), so_regularkey(key), tid);

    tracker.end_op(tid);

    return res;
}

template <class K, class V>
optional<V> SSHashTable<K, V>::remove(K key, int tid)
{

    tracker.start_op(tid);

    bool res = false;
    int bucket = key % size;

    if (buckets[bucket].ui.ptr.load() == nullptr)
    {
        initialize_bucket(bucket,tid);
    }
    if(list_delete(&(buckets[bucket].ui),so_regularkey(key),tid)){
        count.fetch_sub(1);
        res = true;
    }
    
    tracker.end_op(tid);

    return res;
}

template <class K, class V>
bool SSHashTable<K, V>::list_insert(MarkPtr *head, Node *node, int tid)
{

    bool res = false;
    K key = node->key;

    tracker.start_op(tid);
    while (true)
    {
        if (list_find(head, key, tid))
        {
            res = false;
            break;
        }
        else
        {
            //does not exist, insert.
            node->next.ptr.store(curr[tid].ui);
            if (prev[tid].ui->ptr.compare_exchange_strong(curr[tid].ui, node))
            {
                res = true;
                break;
            }
        }
    }
    tracker.end_op(tid);

    return res;
}

template <class K, class V>
optional<V> SSHashTable<K, V>::list_delete(MarkPtr *head, K key, int tid)
{
    bool res = false;
    tracker.start_op(tid);
    while (true)
    {
        if (!list_find(head, key, tid))
        {
            res = false;
            break;
        }
        res = curr[tid].ui->val;
        if (!curr[tid].ui->next.ptr.compare_exchange_strong(next[tid].ui, setMark(next[tid].ui)))
        {
            continue;
        }
        if (prev[tid].ui->ptr.compare_exchange_strong(curr[tid].ui, next[tid].ui))
        {
            tracker.retire(curr[tid].ui, tid);
        }
        else
        {
            list_find(head, key, tid);
        }
        break;
    }
    tracker.end_op(tid);

    return res;
}


template <class K, class V>
bool SSHashTable<K, V>::list_find(MarkPtr* head, K key, int tid)
{
    while (true)
    {
        bool cmark = false;

        prev[tid].ui = head;

        curr[tid].ui = getPtr(prev[tid].ui->ptr.load());

        while (true)
        { //to lock old and curr
            if (curr[tid].ui == nullptr)
                return false;
            next[tid].ui = curr[tid].ui->next.ptr.load();
            cmark = getMark(next[tid].ui);
            next[tid].ui = next[tid].ui;
            auto ckey = curr[tid].ui->key;
            if (prev[tid].ui->ptr.load() != curr[tid].ui)
                break; //retry
            if (!cmark)
            {
                if (ckey >= key)
                    return ckey == key;
                prev[tid].ui = &(curr[tid].ui->next);
            }
            else
            {
                if (prev[tid].ui->ptr.compare_exchange_strong(curr[tid].ui, next))
                {
                    tracker.retire(curr[tid].ui, tid);
                }
                else
                {
                    break; //retry
                }
            }
            curr[tid].ui = next;
        }
    }
}

template <class K, class V>
bool SSHashTable<K, V>::insert(K key, V val, int tid)
{
    assert(0&&"insert not implemented!");
    return 0;}

template <class K, class V>
optional<V> SSHashTable<K, V>::replace(K key, V val, int tid)
{
    assert(0&&"replace not implemented!");
    return 0;
}

#endif

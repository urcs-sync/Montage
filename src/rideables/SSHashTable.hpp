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
    int size = 1000000; //number of buckets for hash table
    int MAX_LOAD = 100000000;
    padded<MarkPtr> *buckets = new padded<MarkPtr>[size] {};
    atomic<int32_t> count = {0};
    RCUTracker<Node> tracker;
    //TODO : must be thread private
    MarkPtr *prev = nullptr;
    Node *curr = nullptr;
    Node *next = nullptr;

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
    SSHashTable(int task_num) : tracker(task_num, 100, 1000, true){};
    ~SSHashTable(){};

    void initialize_bucket(int bucket, int tid);
    int get_parent(int bucket);
    optional<K> reverse_bits(K n);
    optional<K> so_regularkey(K key);
    optional<K> so_dummykey(K key);
    optional<V> get(K key, int tid);
    bool remove(K key, int tid);
    bool put(K key, V val, int tid);
    bool list_insert(MarkPtr *head, Node *node, int tid);
    bool list_find(Node **head, K key, int tid);
    optional<V> list_delete(MarkPtr *head, K key, int tid);

};

template <class K, class V>
int SSHashTable<K, V>::get_parent(int bucket)
{
     int msb = 1<< ((sizeof(int)*8)-__builtin_clz(bucket)-1);
     int result = bucket & ~msb;
     return result;
}

template <class K, class V>
optional<K> SSHashTable<K, V>::reverse_bits(K n)
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
optional<K> SSHashTable<K, V>::so_regularkey(K key)
{
    return reverse_bits(key | (1ULL << (sizeof(K) * 8 - 1)));
}

template <class K, class V>
void SSHashTable<K, V>::initialize_bucket(int bucket, int tid)
{
    tracker.start_op(tid);

    int parent = get_parent(bucket);

    if (buckets[parent] == nullptr)
    {
        initialize_bucket(parent,tid);
    }

    Node dummy = new Node(so_dummykey(bucket), 0, nullptr);
    if (!list_insert(&(buckets[parent]), dummy))
    {
        tracker.retire(dummy, tid);
        delete dummy;
        dummy = curr;
    }

    buckets[parent].compare_exchange_strong(nullptr, dummy);

    tracker.end_op(tid);
}

template <class K, class V>
bool SSHashTable<K, V>::put(K key, V val, int tid)
{
    tracker.start_op(tid);

    bool res = false;
    Node node = new Node(so_regularkey(key), val, nullptr);
    int bucket = key % size;

    if (buckets[bucket] == nullptr)
    {
        initialize_bucket(bucket,tid);
    }
    if (!list_insert(&(buckets[bucket]), node, tid))
    {

        tracker.retire(node, tid);
        delete node;
    } else {

        res = true;
        int csize = size;

        if (atomic_fetch_add(&count,1) / csize > MAX_LOAD)
        {
            (&size).compare_exchange_strong(csize, 2 * csize);
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

    if (buckets[bucket] == nullptr)
    {
        initialize_bucket(bucket,tid);
    }
    res = list_find(&(buckets[bucket]), so_regularkey(key));

    tracker.end_op(tid);

    return res;
}

template <class K, class V>
bool SSHashTable<K, V>::remove(K key, int tid)
{

    tracker.start_op(tid);

    bool res = false;
    int bucket = key % size;

    if (buckets[bucket] == nullptr)
    {
        initialize_bucket(bucket,tid);
    }
    if(list_delete(&(buckets[bucket]),so_regularkey(key)){
        atomic_fetch_sub(&count);
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
            node->next.ptr.store(curr);
            if (prev->ptr.compare_exchange_strong(curr, node))
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
        res = curr->val;
        if (!curr->next.ptr.compare_exchange_strong(next, setMark(next)))
        {
            continue;
        }
        if (prev->ptr.compare_exchange_strong(curr, next))
        {
            tracker.retire(curr, tid);
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
bool SSHashTable<K, V>::list_find(Node **head, K key, int tid)
{
    while (true)
    {
        bool cmark = false;

        prev = new MarkPtr(*head);

        curr = getPtr(prev->ptr.load());

        while (true)
        { //to lock old and curr
            if (curr == nullptr)
                return false;
            next = curr->next.ptr.load();
            cmark = getMark(next);
            next = getPtr(next);
            auto ckey = curr->key;
            if (prev->ptr.load() != curr)
                break; //retry
            if (!cmark)
            {
                if (ckey >= key)
                    return ckey == key;
                prev = &(curr->next);
            }
            else
            {
                if (prev->ptr.compare_exchange_strong(curr, next))
                {
                    tracker.retire(curr, tid);
                }
                else
                {
                    break; //retry
                }
            }
            curr = next;
        }
    }
}

#endif

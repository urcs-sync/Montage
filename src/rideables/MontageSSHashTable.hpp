#ifndef MONTAGE_SSHASHTABLE_HPP
#define MONTAGE_SSHASHTABLE_HPP
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
#include "Recoverable.hpp"

template <class K, class V>
class MontageSSHashTable : public RMap<K, V>, public Recoverable {
    class Payload : public pds::PBlk{
    public:
        K key;
        V val;
        Payload(K x, V y): key(x), val(y){}
        Payload(const Payload& oth): pds::PBlk(oth), key(oth.key), val(oth.val){}
        void persist(){}
    };
    struct Node;
    struct MarkPtr {
        pds::atomic_lin_var<Node *> ptr;
        MarkPtr(Node *n) : ptr(n){};
        MarkPtr() : ptr(nullptr){};
    };

    struct Node {
        MontageSSHashTable* ds;
        size_t so_k;
        MarkPtr next;
        Payload* payload;// TODO: does it have to be atomic?
        Node(MontageSSHashTable* ds_, size_t so, K k, V v, Node *n=nullptr) : ds(ds_), so_k(so), next(n), payload(ds_->pnew<Payload>(k,v)){};
        Node(MontageSSHashTable* ds_, size_t so) : ds(ds_), so_k(so), next(nullptr), payload(nullptr){};
        ~Node(){
            if(payload)
                ds->preclaim(payload);
        }
        void retire_payload(){
            // call it before END_OP but after linearization point
            assert(payload!=nullptr && "payload shouldn't be null");
            ds->pretire(payload);
        }
        inline V get_val(){
            return (V)payload->val;
        }
        inline K get_key(){
            if(payload)
                return (K)payload->key;
            else
                return K();
        }
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
    MontageSSHashTable(GlobalTestConfig* gtc) : Recoverable(gtc), tracker(gtc->task_num, 100, 1000, true){
        buckets = new padded<MarkPtr>[size.load()] {};
        prev = new padded<MarkPtr*>[gtc->task_num];
        curr = new padded<Node*>[gtc->task_num];
        next = new padded<Node*>[gtc->task_num];
    };
    ~MontageSSHashTable(){};

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    int recover(bool simulated){
        errexit("recover of MontageSSHashTable not implemented.");
        return 0;
    }

    optional<V> get(K key, int tid);
    optional<V> remove(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> replace(K key, V val, int tid);
};

template <class T> 
class MontageSSHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MontageSSHashTable<T,T>(gtc);
    }
};

template <class K, class V>
void MontageSSHashTable<K, V>::initialize_bucket(int bucket, int tid)
{
    int parent = get_parent(bucket);

    if (parent!=0 && buckets[parent].ui.ptr.load(this) == nullptr) {
        initialize_bucket(parent,tid);
    }

    Node* dummy = new Node(this,so_dummykey(bucket));
    if (!list_insert(&(buckets[parent].ui), dummy, tid)) {
        delete dummy;
        dummy = curr[tid].ui;
    }

    Node* expected = nullptr;
    buckets[parent].ui.ptr.CAS(this, expected, dummy);
}

template <class K, class V>
bool MontageSSHashTable<K, V>::insert(K key, V val, int tid)
{
    size_t hashed = myhash(key);
    bool res = false;
    Node* node = new Node(this, so_regularkey(hashed), key, val, nullptr);
    int bucket = hashed % size;

    tracker.start_op(tid);
    if (buckets[bucket].ui.ptr.load(this) == nullptr) {
        initialize_bucket(bucket,tid);
    }
    if (list_insert(&(buckets[bucket].ui), node, tid)) {
        res = true;
        int csize = size.load();
        if (count.fetch_add(1) / csize > MAX_LOAD) {
            size.compare_exchange_strong(csize,csize<<1);
        }
    } else {
        delete node;
    }

    tracker.end_op(tid);

    return res;
}

template <class K, class V>
optional<V> MontageSSHashTable<K, V>::get(K key, int tid)
{
    size_t hashed = myhash(key);
    optional<V> res = {};
    int bucket = hashed % size;

    tracker.start_op(tid);
    if (buckets[bucket].ui.ptr.load(this) == nullptr) {
        initialize_bucket(bucket,tid);
    }
    if(list_find(&(buckets[bucket].ui), so_regularkey(hashed), key, tid)){
        res = curr[tid].ui->get_val();
    }

    tracker.end_op(tid);

    return res;
}

template <class K, class V>
optional<V> MontageSSHashTable<K, V>::remove(K key, int tid)
{
    size_t hashed = myhash(key);
    optional<V> res;
    int bucket = hashed % size;

    tracker.start_op(tid);
    if (buckets[bucket].ui.ptr.load(this) == nullptr) {
        initialize_bucket(bucket,tid);
    }
    if(res=list_delete(&(buckets[bucket].ui), so_regularkey(hashed), key, tid)){
        count.fetch_sub(1);
    }

    tracker.end_op(tid);

    return res;
}

template <class K, class V>
bool MontageSSHashTable<K, V>::list_find(MarkPtr* head, size_t so_k, K key, int tid)
{
    while (true){
        bool cmark = false;
        prev[tid].ui = head;
        curr[tid].ui = getPtr(prev[tid].ui->ptr.load(this));
        while (true) {
            if (curr[tid].ui == nullptr)
                return false;
            next[tid].ui = curr[tid].ui->next.ptr.load(this);
            cmark = getMark(next[tid].ui);
            next[tid].ui = getPtr(next[tid].ui);
            auto ckey = curr[tid].ui->so_k;
            if (prev[tid].ui->ptr.load(this) != curr[tid].ui)
                break; //retry
            if (!cmark) {
                if (ckey > so_k) return false;
                if (ckey == so_k && curr[tid].ui->get_key()==key) return true;
                prev[tid].ui = &(curr[tid].ui->next);
            } else {
                if (prev[tid].ui->ptr.CAS(this,curr[tid].ui, next[tid].ui)) {
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
bool MontageSSHashTable<K, V>::list_insert(MarkPtr *head, Node *node, int tid){
    bool res = false;
    K key = node->get_key();

    while (true) {
        if (list_find(head, node->so_k, key, tid)) {
            res = false;
            break;
        } else {
            //does not exist, insert.
            node->next.ptr.store(this,curr[tid].ui);
            if (prev[tid].ui->ptr.CAS_verify(this, curr[tid].ui, node)) {
                res = true;
                break;
            }
        }
    }
    return res;
}

template <class K, class V>
optional<V> MontageSSHashTable<K, V>::list_delete(MarkPtr *head, size_t so_k, K key, int tid)
{
    optional<V> res;
    while (true) {
        if (!list_find(head, so_k, key, tid)) {
            res={};
            break;
        }
        res = curr[tid].ui->get_val();
        curr[tid].ui->retire_payload();
        if (!curr[tid].ui->next.ptr.CAS_verify(this,next[tid].ui, setMark(next[tid].ui))) {
            continue;
        }
        if (prev[tid].ui->ptr.CAS(this,curr[tid].ui, next[tid].ui)) {
            tracker.retire(curr[tid].ui, tid);
        } else {
            list_find(head, so_k, key, tid);
        }
        break;
    }
    return res;
}

template <class K, class V>
optional<V> MontageSSHashTable<K, V>::put(K key, V val, int tid)
{
    optional<V> res;
    assert(0&&"insert not implemented!");
    return res;
}

template <class K, class V>
optional<V> MontageSSHashTable<K, V>::replace(K key, V val, int tid)
{
    optional<V> res;
    assert(0&&"replace not implemented!");
    return res;
}

/* Specialization for strings */
#include <string>
#include "InPlaceString.hpp"
template <>
class MontageSSHashTable<std::string, std::string>::Payload : public pds::PBlk{
public:
    pds::InPlaceString<TESTS_KEY_SIZE> key;
    pds::InPlaceString<TESTS_VAL_SIZE> val;
    Payload(std::string k, std::string v) : key(this, k), val(this, v){}
    Payload(const Payload& oth) : pds::PBlk(oth), key(this, oth.key), val(this, oth.val){}
    void persist(){}
};
#endif

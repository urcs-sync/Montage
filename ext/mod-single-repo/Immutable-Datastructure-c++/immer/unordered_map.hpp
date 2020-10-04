/*
 List-based hash table.
*/
#pragma once

#include <cassert>
#include <memory>
#include <functional>
#include <immer/memory_policy.hpp>
#include <string>
#include <cstring>
#include <mutex>

namespace immer {
template<size_t KEY_SIZE = 32, size_t VAL_SIZE = 1024>
class kv_list
{
    using MemoryPolicy = default_memory_policy;
    using memory      = MemoryPolicy;
    using heap        = typename memory::heap::type;
    struct Item
    {
        void* operator new(size_t size) {
            return heap::allocate(size);
        }

        void operator delete(void* data) {
            heap::deallocate(sizeof(Item),data);
        }

        Item(std::string k, std::string v, const Item* const &tail) : _next(tail) {
            memcpy(_key,k.data(),k.size());
            _key[k.size()]='\0';
            NVM_PERSIST_NOW(_key, k.size()+1);
            memcpy(_val,v.data(),v.size());
            NVM_PERSIST_NOW(_val, v.size());
        }
        Item(char* k, char* v, const Item* const &tail) : _next(tail) {
            size_t k_len = strlen(k);
            memcpy(_key,k,k_len+1);
            NVM_PERSIST_NOW(_key, k_len+1);
            size_t v_len = strlen(v);// should be 1023
            memcpy(_val,v,v_len+1);
            NVM_PERSIST_NOW(_val, v_len+1);
        }
        ~Item(){
        }
        char _key[KEY_SIZE];
        char _val[VAL_SIZE];
        const Item* _next;
    };
    friend Item;
    explicit kv_list(const Item* const & items) : _head(items) {}

    public:

    void* operator new(size_t size) {
        return heap::allocate(size);
    }

    void operator delete(void* data) {
        heap::deallocate(sizeof(kv_list),data);
    }

    // Empty kv_list
    kv_list() : _head(nullptr)  {}
    // Cons
    kv_list(std::string k, std::string v, kv_list const & tail) {
        _head = new Item(k, v, tail._head);
        NVM_PERSIST_NOW(_head, sizeof(Item));
    }
    kv_list(char* k, char* v, kv_list const & tail) {
        _head = new Item(k, v, tail._head);
        NVM_PERSIST_NOW(_head, sizeof(Item));
    }
    kv_list(const kv_list& list){
        this->_head = list._head;
        if(_head!=nullptr)
            NVM_PERSIST_NOW(_head, sizeof(Item));
    }

    bool isEmpty() const { return !_head; } // conversion to bool
    kv_list popped_front() const
    {
        assert(!isEmpty());
        return kv_list(_head->_next);
    }

    // Additional utilities
    kv_list prepended(std::string k, std::string v) const
    {
        return kv_list(k,v, *this);
    }

    kv_list put(std::string k,std::string v) const
    {
        // insert k/v into list in ascending order, with duplicate k replaced
        int cmp;
        if (isEmpty() || (cmp = strncmp(_head->_key,k.data(),k.size())) > 0)
            return prepended(k, v);
        else if(cmp == 0)
            return popped_front().prepended(k,v);
        else
            return kv_list(_head->_key, _head->_val, popped_front().put(k,v));
    }

    kv_list* putPtr(std::string k,std::string v) const
    {
        // insert k/v into list in ascending order, with duplicate k replaced
        kv_list* return_list;
        int cmp;
        if (isEmpty() || (cmp = strncmp(_head->_key,k.data(),k.size())) > 0)
            return_list = new kv_list( prepended(k, v) );
        else if(cmp == 0)
            return_list = new kv_list( popped_front().prepended(k,v) );
        else
            return_list = new kv_list(_head->_key, _head->_val, popped_front().put(k,v));

        NVM_PERSIST_NOW(return_list, sizeof(kv_list));
        return return_list;
    }
    std::string find(const std::string& k){
        const Item* i = _head; 
        while(true){
            int cmp;
            if(i == nullptr || (cmp = strncmp(_head->_key,k.data(),k.size())) > 0)
                return std::string();
            else if(cmp == 0)
                return std::string(i->_val);
            i = i->_next;
        }
    }
    void delete_item_until(std::string k){
        // reclaiming all items until k
        // since put() replaces the same key, we also delete item with k
        const Item* i = _head; 
        const Item* old;
        while(true){
            int cmp;
            if(i == nullptr || (cmp = strncmp(i->_key,k.data(),k.size())) > 0)
                break;
            old = i;
            i = i->_next;
            delete old;
        }
    }

    kv_list remove(std::string k) const
    {
        // remove k from list in ascending order, all items smaller than k are duplicated
        int cmp;
        if (isEmpty() || (cmp = strncmp(_head->_key,k.data(),k.size())) > 0)
            return kv_list(*this);
        else if(cmp == 0)
            return popped_front();
        else
            return kv_list(_head->_key, _head->_val, popped_front().remove(k));
    }

    kv_list* removePtr(std::string k) const
    {
        // remove k from list in ascending order, all items smaller than k are duplicated
        kv_list* return_list;
        int cmp;
        if (isEmpty() || (cmp = strncmp(_head->_key,k.data(),k.size())) > 0)
            return_list = new kv_list( *this );
        else if(cmp == 0)
            return_list = new kv_list( popped_front() );
        else
            return_list = new kv_list( popped_front().remove(k) );

        NVM_PERSIST_NOW(return_list, sizeof(kv_list));
        return return_list;
    }
  private:
    const Item* _head;
};

template <size_t   BucketSize    = 1000000, size_t KEY_SIZE = 32, size_t VAL_SIZE = 1024>
class unordered_map
{
    using MemoryPolicy = default_memory_policy;
    using memory      = MemoryPolicy;
    using heap        = typename memory::heap::type;
    struct Bucket{
        kv_list<KEY_SIZE,VAL_SIZE>* head;
        std::mutex lock;
    }__attribute__((aligned(64)));

public:
    void* operator new(size_t size) {
        return heap::allocate(size);
    }

    void operator delete(void* data) {
        heap::deallocate(sizeof(unordered_map),data);
    }

    unordered_map(){
        for(size_t i = 0; i < BucketSize; i++){
            _buckets[i].head = new kv_list<KEY_SIZE,VAL_SIZE>();
        }
    }
    void set(std::string k, std::string v){
        size_t hash_idx = _hash(k)%BucketSize;
        std::lock_guard<std::mutex> lk(_buckets[hash_idx].lock);
        auto* old_list = _buckets[hash_idx].head;
        _buckets[hash_idx].head = 
                _buckets[hash_idx].head->putPtr(k, v);
        NVM_PERSIST_NOW(&_buckets[hash_idx], sizeof(Bucket));
        _mm_sfence();
        old_list->delete_item_until(k);
        delete old_list;
    }

    // fail and return false if key exists; otherwise set and return true
    bool insert(std::string k, std::string v){
        size_t hash_idx = _hash(k)%BucketSize;
        std::lock_guard<std::mutex> lk(_buckets[hash_idx].lock);
        auto* old_list = _buckets[hash_idx].head;
        auto ret = old_list->find(k);
        if(ret.size()!=0) return false;
        _buckets[hash_idx].head = 
                _buckets[hash_idx].head->putPtr(k, v);
        NVM_PERSIST_NOW(&_buckets[hash_idx], sizeof(Bucket));
        _mm_sfence();
        old_list->delete_item_until(k);
        delete old_list;
        return true;
    }

    std::string find(std::string k){
        size_t hash_idx = _hash(k)%BucketSize;
        std::lock_guard<std::mutex> lk(_buckets[hash_idx].lock);
        auto ret = _buckets[hash_idx].head->find(k);
        return ret;
    }
    void remove(std::string k){
        size_t hash_idx = _hash(k)%BucketSize;
        std::lock_guard<std::mutex> lk(_buckets[hash_idx].lock);
        auto* old_list = _buckets[hash_idx].head;
        _buckets[hash_idx].head = 
                _buckets[hash_idx].head->removePtr(k);
        NVM_PERSIST_NOW(&_buckets[hash_idx], sizeof(Bucket));
        _mm_sfence();
        old_list->delete_item_until(k);
        delete old_list;
    }
    
private:
    // static unordered_map check(kv_list<K,T> const & f, kv_list<K,T> const & r)
    // {
    //     if (f.isEmpty())
    //     {
    //         if (!r.isEmpty())
    //         return unordered_map(reversed(r), kv_list<K,T>());
    //     }
    //     return unordered_map(f, r);
    // }
    Bucket _buckets[BucketSize];
    std::hash<std::string> _hash;
};
} // namespace immer

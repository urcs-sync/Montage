#ifndef Mne_HASHTALE_HPP
#define Mne_HASHTALE_HPP

// use NVM in the map
#define IMMER_USE_NVM true

#include "TestConfig.hpp"
#include "RMap.hpp"
#include "ConcurrentPrimitives.hpp"
#include <mutex>
#include <functional>
#include <string>

#include "mnemosyne.h"
#include "mtm.h"
#include "pmalloc.h"
#define TM_SAFE __attribute__((transaction_safe))
#define PTx     __transaction_atomic
//Assuming K V are both std::string. Will be undefined behavior if not.
template<typename K, typename V, size_t idxSize=500000>
class MneHashTable : public RMap<K,V>{
    TM_SAFE
    static void *
    txc_libc_memcpy (char *dest, const char *src, const int bytes)
    {
        const char *p;
        char *q;
        int count;

        {
            for(p = src, q = dest, count = 0; count < bytes; p++, q++, count++)
                *q = *p;
        }

        return dest;
    }
    TM_SAFE
    static int
    txc_libc_strlen (const char *src)
    {
        int count = 0;
        char x = *src;
        while(x != '\0') {
            ++count;
            ++src;
            x = *src;
        }
        return count;
    }
    TM_SAFE
    static int
    txc_libc_strncmp (const char * s1, const char * s2, size_t n)
    {
        unsigned char c1 = '\0';
        unsigned char c2 = '\0';
        if (n >= 4){
            size_t n4 = n >> 2;
            do{
                c1 = (unsigned char) *s1++;
                c2 = (unsigned char) *s2++;
                if (c1 == '\0' || c1 != c2)
                    return c1 - c2;
                c1 = (unsigned char) *s1++;
                c2 = (unsigned char) *s2++;
                if (c1 == '\0' || c1 != c2)
                    return c1 - c2;
                c1 = (unsigned char) *s1++;
                c2 = (unsigned char) *s2++;
                if (c1 == '\0' || c1 != c2)
                    return c1 - c2;
                c1 = (unsigned char) *s1++;
                c2 = (unsigned char) *s2++;
                if (c1 == '\0' || c1 != c2)
                    return c1 - c2;
            } while (--n4 > 0);
            n &= 3;
        }
        while (n > 0) {
            c1 = (unsigned char) *s1++;
            c2 = (unsigned char) *s2++;
            if (c1 == '\0' || c1 != c2)
                return c1 - c2;
            n--;
        }
        return c1 - c2;
    }

    class Node{
    public:
        char* key;
        char* val;
        Node* next;
        Node(const char* k, size_t k_size, const char* v, size_t v_size, Node* n){
            PTx{
                key = (char*)pmalloc(k_size+1);
                txc_libc_memcpy(key, k, k_size);
                key[k_size]='\0';
                val = (char*)pmalloc(v_size+1);
                txc_libc_memcpy(val, v, v_size);
                val[v_size]='\0';
                next = n;
            }
        }
        Node(){
        }
        ~Node(){
        }
        static void* operator new(std::size_t sz){
            void* ret;
            PTx{ret = pmalloc(sz);}
            return ret;
        }
        static void* operator new[](std::size_t sz){
            void* ret;
            PTx{ret = pmalloc(sz);}
            return ret;
        }
        static void operator delete(void* ptr, std::size_t sz){
            PTx{pfree(ptr);}
            return;
        }
        static void operator delete[](void* ptr, std::size_t sz){
            PTx{pfree(ptr);}
            return;
        }
    };
    
    padded<Node>* buckets;
    std::hash<K> myhash;
public:
    static void* operator new(std::size_t sz){
        void* ret;
        PTx{ret = pmalloc(sz);}
        return ret;
    }
    static void operator delete(void* ptr, std::size_t sz){
        PTx{pfree(ptr);}
        return;
    }
    MneHashTable(GlobalTestConfig* gtc){
        PTx{
            buckets = (padded<Node>*)pmalloc(sizeof(padded<Node>)*idxSize);
            for(size_t i = 0; i<idxSize;i++){
                new(buckets+i) padded<Node>();
            }
        }
    }
    MneHashTable(){
        delete[] buckets;
    }
    struct cstr_holder{
        char* cstr;
        cstr_holder(char* s):cstr(s){};
        ~cstr_holder(){free(cstr);}
    };

    optional<V> get(K key, int tid){
        size_t idx = myhash(key)%idxSize;
        Node* prev = &buckets[idx].ui;
        optional<V> ret = {};
        char* _ret = (char*)malloc(5000);
        cstr_holder holder(_ret);
        bool exist = false;
        size_t key_size = key.size();
        const char* key_data = key.data();
        PTx{
            Node* cur = prev->next;
            while(true){
                if(cur==nullptr){ // end of the list w/o finding key
                    break;
                }
            
                else{ // still in middle of list
                    int cmp = txc_libc_strncmp(cur->key,key_data,key_size);
                    if(cmp==0){ // found key
                        txc_libc_memcpy(_ret, cur->val, txc_libc_strlen(cur->val)+1);
                        exist = true;
                        break;
                    }
                    else if(cmp > 0){ // found spot in list
                        break;
                    }
                    else{ // keep going
                        prev = cur;
                        cur = cur->next;
                    }
                }
            }
        }
        if(exist)
            ret = std::string(_ret);
        return ret;
    }

    optional<V> put(K key, V val, int tid){
        size_t idx = myhash(key)%idxSize;
        optional<V> ret = {};
        Node* prev = &buckets[idx].ui;
        char* _ret = (char*)malloc(5000);
        cstr_holder holder(_ret);
        bool exist = false;
        size_t key_size = key.size();
        const char* key_data = key.data();
        size_t val_size = val.size();
        const char* val_data = val.data();
        PTx{
            Node* cur = prev->next; // first real Node
            while(true){
                // when we enter the loop,
                // we hold a lock on "prev"
                // and we've assigned "cur"
                
                if(cur==nullptr){ // end of the list w/o finding key
                    prev->next = new Node(key_data,key_size,val_data,val_size,nullptr);
                    break;
                }
                
                else{ // still in middle of list
                    int cmp = txc_libc_strncmp(cur->key,key_data,key_size);
                    if(cmp==0){ // found key
                        txc_libc_memcpy(_ret, cur->val, txc_libc_strlen(cur->val)+1);
                        exist = true;
                        txc_libc_memcpy(cur->val, val_data, val_size);
                        break;
                    }
                    else if(cmp>0){ // found spot in list
                        prev->next = new Node(key_data,key_size,val_data,val_size,nullptr);
                        break;				
                    }
                    else{ // keep going
                        prev = cur;
                        cur = cur->next;
                    }
                }
            }
        
        }
        if(exist)
            ret = std::string(_ret);
        return ret;
    }

    bool insert(K key, V val, int tid){
        size_t idx = myhash(key)%idxSize;
        bool ret = false;
        Node* prev = &buckets[idx].ui;
        size_t key_size = key.size();
        const char* key_data = key.data();
        size_t val_size = val.size();
        const char* val_data = val.data();
        PTx{
            Node* cur = prev->next; // first real Node
            while(true){
                // when we enter the loop,
                // we hold a lock on "prev"
                // and we've assigned "cur"
                
                if(cur==nullptr){ // end of the list w/o finding key
                    prev->next = new Node(key_data,key_size,val_data,val_size,nullptr);
                    ret = true;
                    break;
                }
                
                else{ // still in middle of list
                    int cmp = txc_libc_strncmp(cur->key,key_data,key_size);
                    if(cmp==0){ // found key
                        ret = false;
                        break;
                    }
                    else if(cmp>0){ // found spot in list
                        prev->next = new Node(key_data,key_size,val_data,val_size,nullptr);
                        ret = true;
                        break;				
                    }
                    else{ // keep going
                        prev = cur;
                        cur = cur->next;
                    }
                }
            }
        
        }
        return ret;
    }

    optional<V> replace(K key, V val, int tid){
        size_t idx = myhash(key)%idxSize;
        optional<V> ret = {};
        Node* prev = &buckets[idx].ui;
        char* _ret = (char*)malloc(5000);
        cstr_holder holder(_ret);
        bool exist = false;
        size_t key_size = key.size();
        const char* key_data = key.data();
        size_t val_size = val.size();
        const char* val_data = val.data();
        PTx{
            Node* cur = prev->next; // first real Node
            while(true){
                // when we enter the loop,
                // we hold a lock on "prev"
                // and we've assigned "cur"
                
                if(cur==nullptr){ // end of the list w/o finding key
                    break;
                }
                
                else{ // still in middle of list
                    int cmp = txc_libc_strncmp(cur->key,key_data,key_size);
                    if(cmp==0){ // found key
                        txc_libc_memcpy(_ret, cur->val, txc_libc_strlen(cur->val)+1);
                        exist = true;
                        txc_libc_memcpy(cur->val, val_data, val_size);
                        break;
                    }
                    else if(cmp>0){ // found spot in list
                        break;				
                    }
                    else{ // keep going
                        prev = cur;
                        cur = cur->next;
                    }
                }
            }
        
        }

        if(exist)
            ret = std::string(_ret);
        return ret;
    }

    optional<V> remove(K key, int tid){
        size_t idx = myhash(key)%idxSize;
        optional<V> ret = {};
        Node* toDelete = nullptr;
        Node* prev = &buckets[idx].ui;
        char* _ret = (char*)malloc(5000);
        cstr_holder holder(_ret);
        bool exist = false;
        size_t key_size = key.size();
        const char* key_data = key.data();

        PTx{
            Node* cur = prev->next; // first real Node
            while(true){
                // when we enter the loop,
                // we hold a lock on "prev"
                // and we've assigned "cur"
                
                if(cur==nullptr){ // end of the list w/o finding key
                    break;
                }
                
                else{ // still in middle of list
                    int cmp = txc_libc_strncmp(cur->key,key_data,key_size);
                    if(cmp==0){ // found key
                        prev->next=cur->next;
                        txc_libc_memcpy(_ret, cur->val, txc_libc_strlen(cur->val)+1);
                        exist = true;
                        toDelete=cur;
                        break;
                    }
                    else if(cmp>0){ // found spot in list
                        break;				
                    }
                    else{ // keep going
                        prev = cur;
                        cur = cur->next;
                    }
                }
            }
        
        }
        if(toDelete!=nullptr)
            delete toDelete;
        if(exist)
            ret = std::string(_ret);
        return ret;
    }
};

template <class T> 
class MneHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MneHashTable<T,T>(gtc);
    }
};

#endif
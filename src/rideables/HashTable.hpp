#ifndef HASHTALE_HPP
#define HASHTALE_HPP

// transient hash table
#include "TestConfig.hpp"
#include "RMap.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include <mutex>

template<typename K, typename V, size_t idxSize=1000000>
class DRAMHashTable : public RMap<K,V>{
public:
    struct ListNode{
        K key;
        V val;
        // Transient-to-transient pointers
        ListNode* next = nullptr;
        ListNode() : key(""), val("") {}
        ListNode(K k, V v) : key(k), val(v){ }
        inline K get_key(){
            return key;
        }
        inline V get_val(){
            return val;
        }
        inline void set_val(V v){
            val = v;
        }
        ~ListNode(){ }
    };
    struct Bucket{
        std::mutex lock;
        ListNode head;
        Bucket():head(){};
    }__attribute__((aligned(CACHELINE_SIZE)));

    std::hash<K> hash_fn;
    Bucket buckets[idxSize];

    DRAMHashTable(GlobalTestConfig* gtc){ };


    optional<V> get(K key, int tid){
        optional<V> ret={};
        size_t idx=hash_fn(key)%idxSize;
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        ListNode* curr = buckets[idx].head.next;
        while(curr){
            if (curr->get_key() == key){
                ret = curr->get_val();
                break;
            }
            curr = curr->next;
        }
        return ret;
    }

    optional<V> put(K key, V val, int tid){
        optional<V> ret={};
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(key, val);
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        ListNode* curr = buckets[idx].head.next;
        ListNode* prev = &buckets[idx].head;
        while(curr){
            K curr_key = curr->get_key();
            if (curr_key == key){
                ret = curr->get_val();
                curr->set_val(val);
                delete new_node;
                return ret;
            } else if (curr_key > key){
                new_node->next = curr;
                prev->next = new_node;
                return ret;//empty
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        prev->next = new_node;
        return ret;
    }

    bool insert(K key, V val, int tid){
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(key, val);
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        ListNode* curr = buckets[idx].head.next;
        ListNode* prev = &buckets[idx].head;
        while(curr){
            K curr_key = curr->get_key();
            if (curr_key == key){
                delete new_node;
                return false;
            } else if (curr_key > key){
                new_node->next = curr;
                prev->next = new_node;
                return true;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        prev->next = new_node;
        return true;
    }

    optional<V> replace(K key, V val, int tid){
        assert(false && "replace not implemented yet.");
        return {};
    }

    optional<V> remove(K key, int tid){
        optional<V> ret = {};
        size_t idx=hash_fn(key)%idxSize;
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        ListNode* curr = buckets[idx].head.next;
        ListNode* prev = &buckets[idx].head;
        while(curr){
            K curr_key = curr->get_key();
            if (curr_key == key){
                ret = curr->get_val();
                prev->next = curr->next;
                delete(curr);
                return ret;
            } else if (curr_key > key){
                return ret;//empty
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        return ret;
    }
};

/* NVM hash table without flush&fence, for strings only */
#include <string>
#include "PString.hpp"
template <size_t idxSize=1000000>
class NVMHashTable : public RMap<std::string,std::string>{
public:
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
    struct ListNode{
        char key[TESTS_KEY_SIZE];
        char val[TESTS_VAL_SIZE];
        // Transient-to-transient pointers
        ListNode* next = nullptr;
        ListNode() {}
        ListNode(const std::string& k, const std::string& v){
            memcpy(key, k.data(), k.size());
            key[k.size()]='\0';
            memcpy(val, v.data(), v.size());
         }
        inline std::string get_key(){
            return string(key);
        }
        inline std::string get_val(){
            return string(val);
    }
        inline void set_val(const std::string& v){
            memcpy(val, v.data(), v.size());
        }
        ~ListNode(){ }
        void* operator new(size_t size){
            void* ret = RP_malloc(size);
            if (!ret){
                cerr << "Persistent::new failed: no free memory" << endl;
                exit(1);
            }
            return ret;
        }
    	void operator delete(void * p) {
            RP_free(p); 
        } 
    };
    struct Bucket{
        std::mutex lock;
        ListNode head;
        Bucket():head(){};
    }__attribute__((aligned(CACHELINE_SIZE)));

    std::hash<std::string> hash_fn;
    Bucket* buckets;

    NVMHashTable(GlobalTestConfig* gtc){ 
        Persistent::init();
        buckets = (Bucket*)RP_malloc(sizeof(Bucket)*idxSize);
        new (buckets) Bucket [idxSize] ();
    };
    ~NVMHashTable(){
        Persistent::finalize();
    }

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Persistent::init_thread(gtc, ltc);
    }

    optional<std::string> get(std::string key, int tid){
        optional<std::string> ret={};
        size_t idx=hash_fn(key)%idxSize;
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        ListNode* curr = buckets[idx].head.next;
        while(curr){
            int cmp = strncmp(curr->key, key.data(), TESTS_KEY_SIZE);
            if (cmp == 0){
                ret = curr->get_val();
                break;
            }
            curr = curr->next;
        }
        return ret;
    }

    optional<std::string> put(std::string key, std::string val, int tid){
        optional<std::string> ret={};
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(key, val);
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        ListNode* curr = buckets[idx].head.next;
        ListNode* prev = &buckets[idx].head;
        while(curr){
            int cmp = strncmp(curr->key, key.data(), TESTS_KEY_SIZE);
            if (cmp == 0){
                ret = curr->get_val();
                curr->set_val(val);
                delete new_node;
                return ret;
            } else if (cmp > 0){
                new_node->next = curr;
                prev->next = new_node;
                return ret;//empty
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        prev->next = new_node;
        return ret;
    }

    bool insert(std::string key, std::string val, int tid){
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(key, val);
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        ListNode* curr = buckets[idx].head.next;
        ListNode* prev = &buckets[idx].head;
        while(curr){
            int cmp = strncmp(curr->key, key.data(), TESTS_KEY_SIZE);
            if (cmp == 0){
                delete new_node;
                return false;
            } else if (cmp > 0){
                new_node->next = curr;
                prev->next = new_node;
                return true;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        prev->next = new_node;
        return true;
    }

    optional<std::string> replace(std::string key, std::string val, int tid){
        assert(false && "replace not implemented yet.");
        return {};
    }

    optional<std::string> remove(std::string key, int tid){
        optional<std::string> ret = {};
        size_t idx=hash_fn(key)%idxSize;
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        ListNode* curr = buckets[idx].head.next;
        ListNode* prev = &buckets[idx].head;
        while(curr){
            int cmp = strncmp(curr->key, key.data(), TESTS_KEY_SIZE);
            if (cmp == 0){
                ret = curr->get_val();
                prev->next = curr->next;
                delete(curr);
                return ret;
            } else if (cmp > 0){
                return ret;//empty
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        return ret;
    }
};

#ifndef PLACE_DRAM
  #define PLACE_DRAM 1
  #define PLACE_NVM 2
#endif
template <class T, int place = PLACE_DRAM> 
class HashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        if(place == PLACE_DRAM)
            return new DRAMHashTable<T,T>(gtc);
        else
            return new NVMHashTable<>(gtc);
    }
};

#endif
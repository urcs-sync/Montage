#ifndef PRONTO_HASHTABLE_HPP
#define PRONTO_HASHTABLE_HPP

#include "TestConfig.hpp"
#include "RMap.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include <mutex>
#include <array>
#include "savitar.hpp"
#include <uuid/uuid.h>


class ProntoHashTable : public PersistentObject,  public RMap<std::string, std::string> {
    typedef char * T;
public:
    struct ListNode{
        char key[TESTS_KEY_SIZE];
        char val[TESTS_VAL_SIZE];
        // Transient-to-transient pointers
        ListNode* next = nullptr;
        ListNode(std::string k="", std::string v="", ListNode* n=nullptr) : next(n){
            strncpy(key,k.data(),TESTS_KEY_SIZE);
            strncpy(val,v.data(),TESTS_VAL_SIZE);
        }
        ~ListNode(){ }
    };
    struct Bucket{
        std::mutex lock;
        ListNode head;
        Bucket():head(){};
    }__attribute__((aligned(64)));

    ProntoHashTable(uuid_t id) : PersistentObject(id) {
        for (unsigned b = 0; b < idxSize; b++) {
            void *t = alloc->alloc(sizeof(Bucket));
            Bucket *obj = (Bucket *)t;
            buckets[b] = new(obj) Bucket();
        }
    }
    optional<std::string> get(std::string key, int tid){
        optional<std::string> ret={};
        unsigned idx = hash_fn(key) % idxSize;
        std::lock_guard<std::mutex> lk(buckets[idx]->lock);
        ListNode* curr = buckets[idx]->head.next;
        while(curr){
            if (strcmp(curr->key, key.data()) == 0){
                ret = std::string(curr->val);
                break;
            }
            curr = curr->next;
        }
        return ret;
    }

    // return true if key doesn't exist; false if it exists and val will not be replaced
    bool insert(std::string key, std::string val, int tid){
        // <compiler>
        Savitar_thread_notify(4, this, InsertTag, key.data(), val.data());
        // </compiler>
        int ret = -1;
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = (ListNode*)alloc->alloc(sizeof(ListNode));
        new (new_node) ListNode(key, val, nullptr);
        std::lock_guard<std::mutex> lk(buckets[idx]->lock);
        ListNode* curr = buckets[idx]->head.next;
        ListNode* prev = &buckets[idx]->head;

        while(curr){
            int cmp = strcmp(curr->key, key.data());
            if (cmp == 0){
                // curr->val = val;
                ret = 0;
                alloc->dealloc(new_node);
                break;
            } else if (cmp > 0){
                new_node->next = curr;
                prev->next = new_node;
                ret = 1;
                break;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        if(ret == -1) {
            new_node->next = curr;
            prev->next = new_node;
            ret = 1;
        }
        // <compiler>
        Savitar_thread_wait(this, this->log);
        // </compiler>
        return (bool)ret;
    }

    optional<std::string> remove(std::string key, int tid){
        // <compiler>
        Savitar_thread_notify(3, this, RemoveTag, key.data());
        // </compiler>
        optional<std::string> ret={};
        size_t idx=hash_fn(key)%idxSize;
        std::lock_guard<std::mutex> lk(buckets[idx]->lock);
        ListNode* curr = buckets[idx]->head.next;
        ListNode* prev = &buckets[idx]->head;
        while(curr){
            int cmp = strcmp(curr->key, key.data());
            if (cmp == 0){
                prev->next = curr->next;
                ret = std::string(curr->val);
                // alloc->dealloc(curr->key);
                // alloc->dealloc(curr->val);
                alloc->dealloc(curr);
                // alloc->dealloc(key);
                break;
            } else if (cmp > 0){
                break;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        // <compiler>
        Savitar_thread_wait(this, this->log);
        // </compiler>
        return ret;
    }

    optional<std::string> replace(std::string key, std::string val, int tid){
        assert(false && "replace not implemented yet.");
        return {};
    }

    optional<std::string> put(std::string key, std::string val, int tid){
        assert(false && "put not implemented yet.");
        return {};
    }
    // <compiler>
    ProntoHashTable() : PersistentObject(true) { }

    static PersistentObject *BaseFactory(uuid_t id) {
        ObjectAlloc *alloc = GlobalAlloc::getInstance()->newAllocator(id);
        void *temp = alloc->alloc(sizeof(ProntoHashTable));
        ProntoHashTable *obj = (ProntoHashTable *)temp;
        ProntoHashTable *object = new (obj) ProntoHashTable(id);
        return object;
    }

    static PersistentObject *RecoveryFactory(NVManager *m, CatalogEntry *e) {
        return BaseFactory(e->uuid);
    }

    static ProntoHashTable *Factory(uuid_t id) {
        NVManager &manager = NVManager::getInstance();
        manager.lock();
        ProntoHashTable *obj =
            (ProntoHashTable *)manager.findRecovered(id);
        if (obj == NULL) {
            obj = static_cast<ProntoHashTable *>(BaseFactory(id));
            manager.createNew(classID(), obj);
        }
        manager.unlock();
        return obj;
    }

    uint64_t Log(uint64_t tag, uint64_t *args) {
        int vector_size = 0;
        ArgVector vector[4]; // Max arguments of the class

        switch (tag) {
            case InsertTag:
                {
                vector[0].addr = &tag;
                vector[0].len = sizeof(tag);
                vector[1].addr = (void *)args[0];
                vector[1].len = strlen((char *)args[0]) + 1;
                vector[2].addr = (void *)args[1];
                vector[2].len = strlen((char *)args[1]) + 1;
                vector_size = 3;
                }
                break;
            case RemoveTag:
                {
                vector[0].addr = &tag;
                vector[0].len = sizeof(tag);
                vector[1].addr = (void *)args[0];
                vector[1].len = strlen((char *)args[0]) + 1;
                vector_size = 2;
                }
                break;
            default:
                assert(false);
                break;
        }

        return AppendLog(vector, vector_size);
    }

    size_t Play(uint64_t tag, uint64_t *args, bool dry) {
        size_t bytes_processed = 0;
        switch (tag) {
            case InsertTag:
                {
                char *key = (char *)args;
                char *value = (char *)args + strlen(key) + 1;
                if (!dry) insert(std::string(key), std::string(value) ,0);
                bytes_processed = strlen(key) + strlen(value) + 2;
                }
                break;
            case RemoveTag:
                {
                char *key = (char *)args;
                if (!dry) remove(std::string(key),0);
                bytes_processed = strlen(key) + 1;
                }
                break;
            default:
                {
                PRINT("Unknown tag: %zu\n", tag);
                assert(false);
                }
                break;
        }
        return bytes_processed;
    }

    static uint64_t classID() { return __COUNTER__; }
    // </compiler>

private:
    static const size_t idxSize=1000000;
    std::hash<std::string> hash_fn;
    Bucket* buckets[idxSize];
    // <compiler>
    enum MethodTags {
        InsertTag = 1,
        RemoveTag = 2,
    };
    // </compiler>
};

class ProntoHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        uuid_t uuid;
        uuid_generate(uuid);
        return ProntoHashTable::Factory(uuid);
    }
};

#endif
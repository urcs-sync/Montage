#ifndef MOD_HASHTALE_HPP
#define MOD_HASHTALE_HPP

// use NVM in the map
#define IMMER_USE_NVM true

#include "TestConfig.hpp"
#include "RMap.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include <mutex>
#include <cstring>
#include <cstdio>
#include "immer/unordered_map.hpp"


template<typename K, typename V, size_t idxSize=1000000>
class MODHashTable : public RMap<K,V>{
    using map_type = immer::unordered_map<idxSize, TESTS_KEY_SIZE, TESTS_VAL_SIZE>;
    map_type* table;
public:
    MODHashTable(GlobalTestConfig* gtc){
        char* heap_prefix = (char*) malloc(L_cuserid+11);
        strcpy(heap_prefix,"/mnt/pmem/");
        cuserid(heap_prefix+strlen("/mnt/pmem/"));
        nvm_initialize(heap_prefix, 0);
        free(heap_prefix);
        table = new map_type();
    };

    optional<V> get(K key, int tid){
        return table->find(key);
    }

    optional<V> put(K key, V val, int tid){
        table->set(key,val);
        return {};
    }

    bool insert(K key, V val, int tid){
        // CAUTION: due to MOD implementation, insert has to search for 
        // the key, and then go through again, duplicating all nodes if
        // the key doesn't exist
        return table->insert(key,val);
    }

    optional<V> replace(K key, V val, int tid){
        assert(false && "replace not implemented yet.");
        return {};
    }

    optional<V> remove(K key, int tid){
        table->remove(key);
        return {};
    }
};

template <class T> 
class MODHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MODHashTable<T,T>(gtc);
    }
};

#endif
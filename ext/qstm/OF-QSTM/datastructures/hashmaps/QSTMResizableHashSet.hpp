/*
 * Copyright 2017-2018
 *   Andreia Correia <andreia.veiga@unine.ch>
 *   Pedro Ramalhete <pramalhe@gmail.com>
 *   Pascal Felber <pascal.felber@unine.ch>
 *   Nachshon Cohen <nachshonc@gmail.com>
 *
 * This work is published under the MIT license. See LICENSE.txt
 */
#ifndef _QSTM_RESIZABLE_HASH_MAP_H_
#define _QSTM_RESIZABLE_HASH_MAP_H_

#include <string>

#include "ptms/QSTM.hpp"

/**
 * <h1> A Resizable Hash Map for usage with STMs </h1>
 * TODO
 *
 */
template<typename K>
class QSTMResizableHashSet {

private:
    struct Node : public tmbase {
        tmtype<K>     key;
        tmtype<Node*> next {nullptr};
        Node(const K& k) : key{k} { } // Copy constructor for k
    };

    tmtype<uint64_t>                     capacity;
    tmtype<uint64_t>                     sizeHM = 0;
    static constexpr double                       loadFactor = 0.75;
    alignas(128) tmtype<tmtype<Node*>*>      buckets;      // An array of pointers to Nodes


public:
    QSTMResizableHashSet(int maxThreads=0, uint64_t capacity=4) : capacity{capacity} {
        updateTx<bool>([&] () {
            buckets = (tmtype<Node*>*)tmMalloc(capacity*sizeof(tmtype<Node*>));
            for (int i = 0; i < capacity; i++) buckets[i] = nullptr;
            return true;
        });
    }


    ~QSTMResizableHashSet() {
        updateTx<bool>([&] () {
            for(int i = 0; i < capacity; i++){
                Node* node = buckets[i];
                while (node != nullptr) {
                    Node* next = node->next;
//                    tmDelete(node);
                    tmFree(node);
                    node = next;
                }
            }
            tmFree(buckets.load());
            return true;
        });
    }


    static std::string className() { return "QSTM-HashMap"; }


    void rebuild() {
        uint64_t newcapacity = 2*capacity;
        tmtype<Node*>* newbuckets = (tmtype<Node*>*)tmMalloc(newcapacity*sizeof(tmtype<Node*>));
        for (int i = 0; i < newcapacity; i++) newbuckets[i] = nullptr;
        for (int i = 0; i < capacity; i++) {
            Node* node = buckets[i];
            while (node!=nullptr) {
                Node* next = node->next;
                auto h = std::hash<K>{}(node->key) % newcapacity;
                node->next = newbuckets[h];
                newbuckets[h] = node;
                node = next;
            }
        }
        tmFree(buckets);
        buckets = newbuckets;
        capacity = newcapacity;
    }


    /*
     * Adds a node with a key if the key is not present, otherwise replaces the value.
     * If saveOldValue is set, it will set 'oldValue' to the previous value, iff there was already a mapping.
     *
     * Returns true if there was no mapping for the key, false if there was already a value and it was replaced.
     */
    bool innerPut(const K& key) {
        if (sizeHM > capacity*loadFactor) rebuild();
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        Node* prev = node;
        while (true) {
            if (node == nullptr) {
                Node* newnode = tmNew<Node>(key);
                if (node == prev) {
                    buckets[h] = newnode;
                } else {
                    prev->next = newnode;
                }
                sizeHM++;
                return true;  // New insertion
            }
            if (key == node->key) return false;
            prev = node;
            node = node->next;
        }
    }


    /*
     * Removes a key and its mapping.
     * Saves the value in 'oldvalue' if 'saveOldValue' is set.
     *
     * Returns returns true if a matching key was found
     */
    bool innerRemove(const K& key) {
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        Node* prev = node;
        while (true) {
            if (node == nullptr) return false;
            if (key == node->key) {
                if (node == prev) {
                    buckets[h] = node->next;
                } else {
                    prev->next = node->next;
                }
                sizeHM--;
                tmFree(node);
                return true;
            }
            prev = node;
            node = node->next;
        }
    }


    /*
     * Returns true if key is present. Saves a copy of 'value' in 'oldValue' if 'saveOldValue' is set.
     */
    bool innerGet(const K& key) {
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        while (true) {
            if (node == nullptr) return false;
            if (key == node->key) return true;
            node = node->next;
        }
    }


    //
    // Set methods for running the usual tests and benchmarks
    //

	bool bigtxn(K** udarray, int tid, int numElements){
		return updateTx<bool>([this,udarray,tid,numElements] () {
        	uint64_t seed = tid*133 + 1234567890123456781ULL;
			for(int i=0; i<1; i++){ // Specifies number of writes per txn
		        seed = randomLong(seed);
				auto ix = (unsigned int)((seed)%numElements);
				if(innerRemove(*udarray[ix])){
					innerPut(*udarray[ix]);
				}
			}
			return true;
		});
	}

    // Inserts a key only if it's not already present
    bool add(K key, const int tid=0) {
        return updateTx<bool>([&] () {
            return innerPut(key);
        });
    }

    // Returns true only if the key was present
    bool remove(K key, const int tid=0) {
        return updateTx<bool>([&] () {
            return innerRemove(key);
        });
    }

    bool contains(K key, const int tid=0) {
        return readTx<bool>([&] () {
            return innerGet(key);
        });
    }

    // Used only for benchmarks
    void addAll(K** keys, const int size, const int tid=0) {
        for (int i = 0; i < size; i++) add(*keys[i]);
    }
};

#endif /* _TINY_STM_RESIZABLE_HASH_MAP_H_ */

#ifndef _PERSISTENT_TM_RESIZABLE_HASH_MAP_H_
#define _PERSISTENT_TM_RESIZABLE_HASH_MAP_H_

#include <string>

/**
     * An imprecise but fast random number generator
     */
    uint64_t randomLong(uint64_t x) {
        x ^= x >> 12; // a
        x ^= x << 25; // b
        x ^= x >> 27; // c
        return x * 2685821657736338717LL;
    }


/**
 * <h1> A Resizable Hash Map for PTMs </h1>
 */
template<typename K, typename V, typename TM, template <typename> class TMTYPE>
class TMHashMap {

private:
    struct Node {
        TMTYPE<K>     key;
        TMTYPE<V>     val;
        TMTYPE<Node*> next {nullptr};
        Node(const K& k, const V& v) : key{k}, val{v} { } // Copy constructor for k and value
        Node() {}
    };


    TMTYPE<uint64_t>                    capacity;
    TMTYPE<uint64_t>                    sizeHM = 0;
    //TMTYPE<double>					loadFactor = 0.75;
    static constexpr double             loadFactor = 0.75;
    alignas(128) TMTYPE<TMTYPE<Node*>*> buckets;      // An array of pointers to Nodes


public:
    TMHashMap(uint64_t capacity=4) : capacity{capacity} {
		buckets = (TMTYPE<Node*>*)TM::pmalloc(capacity*sizeof(TMTYPE<Node*>));
		for (int i = 0; i < capacity; i++) buckets[i]=nullptr;
    }


    ~TMHashMap() {
		for(int i = 0; i < capacity; i++){
			Node* node = buckets[i];
			while (node!=nullptr) {
				Node* next = node->next;
				TM::tmDelete(node);
				node = next;
			}
		}
		TM::pfree(buckets);
    }


    static std::string className() { return TM::className() + "-HashMap"; }


    void rebuild() {
        uint64_t newcapacity = 2*capacity;
        //printf("increasing capacity to %d\n", newcapacity);
        TMTYPE<Node*>* newbuckets = (TMTYPE<Node*>*)TM::pmalloc(newcapacity*sizeof(TMTYPE<Node*>));
        for (int i = 0; i < newcapacity; i++) newbuckets[i] = nullptr;
        for (int i = 0; i < capacity; i++) {
            Node* node = buckets[i];
            while(node!=nullptr){
                Node* next = node->next;
                auto h = std::hash<K>{}(node->key) % newcapacity;
                node->next = newbuckets[h];
                newbuckets[h] = node;
                node = next;
            }
        }
        TM::pfree(buckets);
        buckets = newbuckets;
        capacity = newcapacity;
    }


    /*
     * Adds a node with a key if the key is not present, otherwise replaces the value.
     * If saveOldValue is set, it will set 'oldValue' to the previous value, iff there was already a mapping.
     *
     * Returns true if there was no mapping for the key, false if there was already a value and it was replaced.
     */
//    bool innerPut(const K& key, const V& value, V& oldValue, const bool saveOldValue) {
    bool innerPut(const K& key, const V& value) {
    	//printf("innerPut %d %d %f\n", sizeHM.pload(), capacity.pload(), loadFactor.pload()*capacity.pload());
        if (sizeHM.pload() > capacity.pload()*loadFactor) rebuild();
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        Node* prev = node;
        while (true) {
            if (node == nullptr) {
                Node* newnode = TM::template tmNew<Node>(key,value);
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
    //bool innerRemove(const K& key, V& oldValue, const bool saveOldValue) {
    bool innerRemove(const K& key) {
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        Node* prev = node;
        while (true) {
            if (node == nullptr) return false;
            if (key == node->key) {
//                if (saveOldValue) oldValue = node->val; // Makes a copy of V
                if (node == prev) {
                    buckets[h] = node->next;
                } else {
                    prev->next = node->next;
                }
                sizeHM--;
                TM::tmDelete(node);
                return true;
            }
            prev = node;
            node = node->next;
        }
    }


    /*
     * Returns true if key is present. Saves a copy of 'value' in 'oldValue' if 'saveOldValue' is set.
     */
    bool innerGet(const K& key, V& oldValue, const bool saveOldValue) {
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        while (true) {
            if (node == nullptr) return false;
            if (key == node->key) {
                return true;
            }
            node = node->next;
        }
    }


    //
    // Set methods for running the usual tests and benchmarks
    //

	bool bigtxn(K** udarray, int tid, int numElements){
		return TM::template updateTx<bool>([this,udarray,tid,numElements] () {
            V notused;
        	uint64_t seed = tid*133 + 1234567890123456781ULL;
			for(int i=0; i<1; i++){ // Specify number of ops per txn for OneFile
		        seed = randomLong(seed);
				auto ix = (unsigned int)((seed)%numElements);
				if(innerRemove(*udarray[ix])){
					innerPut(*udarray[ix],*udarray[ix]);
				}
			}
			return true;
		});
	}
/*
	bool bigreadtxn(K** udarray, int tid, int numElements){
        return TM::template readTx<bool>([this,udarray,tid,numElements] () {
            V notused;
        	uint64_t seed = tid*133 + 1234567890123456781ULL;
			for(int i=0; i<5; i++){
		        seed = randomLong(seed);
				auto ix = (unsigned int)((seed)%numElements);
				if(innerGet(*udarray[ix],notused,false)){
					seed = randomLong(seed);
				}
			}
			return true;
		});
	}
*/
    // Inserts a key only if it's not already present
    bool add(const K& key) {
        return TM::template updateTx<bool>([this,key] () {
            V notused;
            return innerPut(key,key);
        });
    }

    // Returns true only if the key was present
    bool remove(const K& key) {
        return TM::template updateTx<bool>([this,key] () {
            V notused;
            return innerRemove(key);
        });
    }

    bool contains(const K& key) {
        return TM::template readTx<bool>([this,key] () {
            V notused;
            return innerGet(key,notused,false);
        });
    }

    // Used only for benchmarks
    bool addAll(K** keys, const int size) {
        for (int i = 0; i < size; i++) add(*keys[i]);
        return true;
    }
};

#endif /* _PERSISTENT_TM_RESIZABLE_HASH_MAP_H_ */

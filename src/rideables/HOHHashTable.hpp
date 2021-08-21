#ifndef HOH_HASHTALE_HPP
#define HOH_HASHTALE_HPP

#include "TestConfig.hpp"
#include "RMap.hpp"
#include "Recoverable.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include <mutex>

template<typename K, typename V, size_t idxSize=1000000>
class HOHHashTable : public RMap<K,V>, public Recoverable{
public:

    class Payload : public pds::PBlk{
        GENERATE_FIELD(K, key, Payload);
        GENERATE_FIELD(V, val, Payload);
    public:
        Payload(){}
        Payload(K x, V y): m_key(x), m_val(y){}
        Payload(const Payload& oth): pds::PBlk(oth), m_key(oth.m_key), m_val(oth.m_val){}
        void persist(){}
    };

    struct ListNode{
        HOHHashTable* ds;
        // Transient-to-persistent pointer
        Payload* payload = nullptr;
        // Transient-to-transient pointers
        ListNode* next = nullptr;
        std::mutex lock;
        ListNode(){}
        ListNode(HOHHashTable* ds_, K key, V val): ds(ds_){
            payload = ds->pnew<Payload>(key, val);
        }
        K get_key(){
            assert(payload!=nullptr && "payload shouldn't be null");
            return (K)payload->get_key(ds);
        }
        V get_val(){
            assert(payload!=nullptr && "payload shouldn't be null");
            return (V)payload->get_val(ds);
        }
        void set_val(V v){
            assert(payload!=nullptr && "payload shouldn't be null");
            payload = payload->set_val(ds, v);
        }
        ~ListNode(){
            ds->pdelete(payload);
        }
    };
    std::hash<K> hash_fn;
    padded<ListNode>* buckets[idxSize];

    HOHHashTable(GlobalTestConfig* gtc): Recoverable(gtc){ 
        for(size_t i = 0; i < idxSize; i++){
            buckets[i] = new padded<ListNode>();
        }
    }

    int recover(bool simulated){
        errexit("recover of HOHHashTable not implemented");
    }

    optional<V> get(K key, int tid){
        size_t idx=hash_fn(key)%idxSize;
        while(true){
            MontageOpHolder _holder(this);
            try{
                HOHLockHolder holder;
                holder.hold(&buckets[idx]->ui.lock);
                ListNode* curr = buckets[idx]->ui.next;
                while(curr){
                    holder.hold(&curr->lock);
                    if (curr->get_key() == key){
                        return curr->get_val();
                    }
                    curr = curr->next;
                }
                return {};
            } catch(pds::OldSeeNewException& e){
                continue;
            }
        }
    }

    optional<V> put(K key, V val, int tid){
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(this, key, val);
        while(true){
            MontageOpHolder _holder(this);
            try{
                HOHLockHolder holder;
                holder.hold(&buckets[idx]->ui.lock);
                ListNode* curr = buckets[idx]->ui.next;
                ListNode* prev = &buckets[idx]->ui;
                while(curr){
                    holder.hold(&curr->lock);
                    K curr_key = curr->get_key();
                    if (curr_key == key){
                        optional<V> ret = curr->get_val();
                        curr->payload = curr->payload->set_val(val);
                        delete new_node;
                        return ret;
                    } else if (curr_key > key){
                        new_node->next = curr;
                        prev->next = new_node;
                        return {};
                    } else {
                        prev = curr;
                        curr = curr->next;
                    }
                }
                prev->next = new_node;
                return {};
            } catch (pds::OldSeeNewException& e){
                continue;
            }
        }
    }

    bool insert(K key, V val, int tid){
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(this, key, val);
        while(true){
            MontageOpHolder _holder(this);
            try{
                HOHLockHolder holder;
                holder.hold(&buckets[idx]->ui.lock);
                ListNode* curr = buckets[idx]->ui.next;
                ListNode* prev = &buckets[idx]->ui;
                while(curr){
                    holder.hold(&curr->lock);
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
                return {};
            } catch (pds::OldSeeNewException& e){
                continue;
            }
        }
    }

    optional<V> replace(K key, V val, int tid){
        assert(false && "replace not implemented yet.");
        return {};
    }

    optional<V> remove(K key, int tid){
        size_t idx=hash_fn(key)%idxSize;
        while(true){
            MontageOpHolder _holder(this);
            try{
                HOHLockHolder holder;
                holder.hold(&buckets[idx]->ui.lock);
                ListNode* curr = buckets[idx]->ui.next;
                ListNode* prev = &buckets[idx]->ui;
                while(curr){
                    holder.hold(&curr->lock);
                    K curr_key = curr->get_key();
                    if (curr_key == key){
                        optional<V> ret = curr->get_val();
                        prev->next = curr->next;
                        delete(curr);
                        return ret;
                    } else if (curr_key > key){
                        return {};
                    } else {
                        prev = curr;
                        curr = curr->next;
                    }
                }
                return {};
            } catch (pds::OldSeeNewException& e){
                continue;
            }
        }
    }
};

template <class T> 
class HOHHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new HOHHashTable<T,T>(gtc);
    }
};

/* Specialization for strings */
#include <string>
#include "InPlaceString.hpp"
template <>
class HOHHashTable<std::string, std::string, 1000000>::Payload : public pds::PBlk{
    GENERATE_FIELD(pds::InPlaceString<TESTS_KEY_SIZE>, key, Payload);
    GENERATE_FIELD(pds::InPlaceString<TESTS_VAL_SIZE>, val, Payload);

public:
    Payload(std::string k, std::string v) : m_key(this, k), m_val(this, v){}
    void persist(){}
};

#endif

#ifndef MONTAGE_HASHTALE_HPP
#define MONTAGE_HASHTALE_HPP

#include "TestConfig.hpp"
#include "RMap.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include "Recoverable.hpp"
#include <mutex>
#include <omp.h>

using namespace pds;

template<typename K, typename V, size_t idxSize=1000000>
class MontageHashTable : public RMap<K,V>, public Recoverable{
public:

    class Payload : public PBlk{
        GENERATE_FIELD(K, key, Payload);
        GENERATE_FIELD(V, val, Payload);
    public:
        Payload(){}
        Payload(K x, V y): m_key(x), m_val(y){}
        // Payload(const Payload& oth): PBlk(oth), m_key(oth.m_key), m_val(oth.m_val){}
        void persist(){}
    }__attribute__((aligned(CACHELINE_SIZE)));

    struct ListNode{
        MontageHashTable* ds;
        // Transient-to-persistent pointer
        Payload* payload = nullptr;
        // Transient-to-transient pointers
        ListNode* next = nullptr;
        ListNode(){}
        ListNode(MontageHashTable* ds_, K key, V val): ds(ds_){
            payload = ds->pnew<Payload>(key, val);
        }
        ListNode(Payload* _payload) : payload(_payload) {} // for recovery
        K get_key(){
            assert(payload!=nullptr && "payload shouldn't be null");
            // old-see-new never happens for locking ds
            return (K)payload->get_unsafe_key(ds);
        }
        V get_val(){
            assert(payload!=nullptr && "payload shouldn't be null");
            return (V)payload->get_unsafe_val(ds);
        }
        void set_val(V v){
            assert(payload!=nullptr && "payload shouldn't be null");
            payload = payload->set_val(ds, v);
        }
        ~ListNode(){
            if (payload){
                ds->pdelete(payload);
            }
        }
    }__attribute__((aligned(CACHELINE_SIZE)));
    struct Bucket{
        mutex lock;
        ListNode head;
        Bucket():head(){};
    }__attribute__((aligned(CACHELINE_SIZE)));

    std::hash<K> hash_fn;
    Bucket buckets[idxSize];
    GlobalTestConfig* gtc;
    MontageHashTable(GlobalTestConfig* gtc_): Recoverable(gtc_), gtc(gtc_){};

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    optional<V> get(K key, int tid){
        size_t idx=hash_fn(key)%idxSize;
        // while(true){
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        MontageOpHolderReadOnly(this);
            // try{
        ListNode* curr = buckets[idx].head.next;
        while(curr){
            if (curr->get_key() == key){
                return curr->get_val();
            }
            curr = curr->next;
        }
        return {};
            // } catch(OldSeeNewException& e){
                // continue;
            // }
        // }
    }

    optional<V> put(K key, V val, int tid){
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(this, key, val);
        // while(true){
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        MontageOpHolder(this);
        // try{
        ListNode* curr = buckets[idx].head.next;
        ListNode* prev = &buckets[idx].head;
        while(curr){
            K curr_key = curr->get_key();
            if (curr_key == key){
                optional<V> ret = curr->get_val();
                curr->set_val(val);
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
        //     } catch (OldSeeNewException& e){
        //         continue;
        //     }
        // }
    }

    bool insert(K key, V val, int tid){
        size_t idx=hash_fn(key)%idxSize;
        ListNode* new_node = new ListNode(this, key, val);
        // while(true){
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        MontageOpHolder(this);
        // try{
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
        //     } catch (OldSeeNewException& e){
        //         continue;
        //     }
        // }
    }

    optional<V> replace(K key, V val, int tid){
        assert(false && "replace not implemented yet.");
        return {};
    }

    optional<V> remove(K key, int tid){
        size_t idx=hash_fn(key)%idxSize;
        // while(true){
        std::lock_guard<std::mutex> lk(buckets[idx].lock);
        MontageOpHolder(this);
        // try{
        ListNode* curr = buckets[idx].head.next;
        ListNode* prev = &buckets[idx].head;
        while(curr){
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
        //     } catch (OldSeeNewException& e){
        //         continue;
        //     }
        // }
    }

    void clear(){
        for (uint64_t i = 0; i < idxSize; i++){
            ListNode* curr = buckets[i].head.next;
            ListNode* next = nullptr;
            while(curr){
                next = curr->next;
                delete curr;
                curr = next;
            }
            buckets[i].head.next = nullptr;
        }
    }


    int recover(bool simulated){
        if (simulated){
            recover_mode(); // PDELETE --> noop
            // clear transient structures.
            clear();
            online_mode(); // re-enable PDELETE.
        }

        int rec_cnt = 0;
        int rec_thd = 10;
        if (gtc->checkEnv("RecoverThread")){
            rec_thd = stoi(gtc->getEnv("RecoverThread"));
        }
        auto begin = chrono::high_resolution_clock::now();
        std::unordered_map<uint64_t, PBlk*>* recovered = recover_pblks(rec_thd); 
        auto end = chrono::high_resolution_clock::now();
        auto dur = end - begin;
        auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms << "ms getting PBlk(" << recovered->size() << ")" << std::endl;
        std::vector<Payload*> payloadVector;
        payloadVector.reserve(recovered->size());
        begin = chrono::high_resolution_clock::now();
        for (auto itr = recovered->begin(); itr != recovered->end(); itr++){
            rec_cnt++;
            Payload* payload = reinterpret_cast<Payload*>(itr->second);
                        payloadVector.push_back(payload);
        }
        end = chrono::high_resolution_clock::now();
        dur = end - begin;
        auto dur_ms_vec = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms_vec << "ms building vector" << std::endl;
        begin = chrono::high_resolution_clock::now();
        #pragma omp parallel num_threads(rec_thd)
        {
            Recoverable::init_thread(omp_get_thread_num());
            #pragma omp for
            for(size_t i = 0; i < payloadVector.size(); ++i){
                //re-insert payload.
                ListNode* new_node = new ListNode(payloadVector[i]);
                K key = new_node->get_key();
                size_t idx=hash_fn(key)%idxSize;
                std::lock_guard<std::mutex> lk(buckets[idx].lock);
                ListNode* curr = buckets[idx].head.next;
                ListNode* prev = &buckets[idx].head;
                while(curr){
                    K curr_key = curr->get_key();
                    if (curr_key == key){
                        errexit("conflicting keys recovered.");
                    } else if (curr_key > key){
                        new_node->next = curr;
                        prev->next = new_node;
                        break;
                    } else {
                        prev = curr;
                        curr = curr->next;
                    }
                }
                prev->next = new_node;
            }
        }
        end = chrono::high_resolution_clock::now();
        dur = end - begin;
        auto dur_ms_ins = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms_ins << "ms inserting(" << recovered->size() << ")" << std::endl;
        std::cout << "Total time to recover: " << dur_ms+dur_ms_vec+dur_ms_ins << "ms" << std::endl;
        delete recovered;
        return rec_cnt;
    }
};

template <class T> 
class MontageHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MontageHashTable<T,T>(gtc);
    }
};

/* Specialization for strings */
#include <string>
#include "PString.hpp"
template <>
class MontageHashTable<std::string, std::string, 1000000>::Payload : public PBlk{
    GENERATE_FIELD(PString<TESTS_KEY_SIZE>, key, Payload);
    GENERATE_FIELD(PString<TESTS_VAL_SIZE>, val, Payload);

public:
    Payload(const std::string& k, const std::string& v) : m_key(this, k), m_val(this, v){}
    void persist(){}
};

#endif

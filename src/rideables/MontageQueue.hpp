#ifndef MONTAGE_QUEUE_P
#define MONTAGE_QUEUE_P

#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RQueue.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"
#include "Recoverable.hpp"
#include "Recoverable.hpp"
#include <mutex>


template<typename T>
class MontageQueue : public RQueue<T>, public Recoverable{
public:
    class Payload : public pds::PBlk{
        GENERATE_FIELD(T, val, Payload);
        GENERATE_FIELD(uint64_t, sn, Payload); 
    public:
        Payload(){}
        Payload(T v, uint64_t n): m_val(v), m_sn(n){}
        Payload(const Payload& oth): PBlk(oth), m_sn(0), m_val(oth.m_val){}
        void persist(){}
    };

private:
    struct Node{
        MontageQueue* ds;
        Node* next;
        Payload* payload;
        T val; // for debug purpose

        Node(): next(nullptr), payload(nullptr){}; 
        // Node(): next(nullptr){}; 
        Node(MontageQueue* ds_, T v, uint64_t n=0): 
            ds(ds_), next(nullptr), payload(ds_->pnew<Payload>(v, n)), val(v){};
        // Node(T v, uint64_t n): next(nullptr), val(v){};

        void set_sn(uint64_t s){
            assert(payload!=nullptr && "payload shouldn't be null");
            payload->set_unsafe_sn(ds, s);
        }
        T get_val(){
            assert(payload!=nullptr && "payload shouldn't be null");
            // old-see-new never happens for locking ds
            return (T)payload->get_unsafe_val(ds);
            // return val;
        }
        ~Node(){
            ds->pdelete(payload);
        }
    };

public:
    uint64_t global_sn;

private:
    // dequeue pops node from head
    Node* head;
    // enqueue pushes node to tail
    Node* tail;
    std::mutex lock;

public:
    MontageQueue(GlobalTestConfig* gtc): 
        Recoverable(gtc), global_sn(0), head(nullptr), tail(nullptr){
    }

    ~MontageQueue(){};

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    int recover(bool simulated){
        errexit("recover of MontageQueue not implemented.");
        return 0;
    }

    void enqueue(T val, int tid);
    optional<T> dequeue(int tid);
};

template<typename T>
void MontageQueue<T>::enqueue(T val, int tid){
    Node* new_node = new Node(this, val);
    std::lock_guard<std::mutex> lk(lock);
    // no read or write so impossible to have old see new exception
    new_node->set_sn(global_sn);
    global_sn++;
    MontageOpHolder(this);
    if(tail == nullptr) {
        head = tail = new_node;
        return;
    }
    tail->next = new_node;
    tail = new_node;
}

template<typename T>
optional<T> MontageQueue<T>::dequeue(int tid){
    optional<T> res = {};
    // while(true){
    lock.lock();
    MontageOpHolder(this);
    // try {
    if(head == nullptr) {
        lock.unlock();
        return res;
    }
    Node* tmp = head;
    res = tmp->get_val();
    head = head->next;
    if(head == nullptr) {
        tail = nullptr;
    }
    lock.unlock();
    delete(tmp);
    return res;
    //     } catch (OldSeeNewException& e) {
    //         continue;
    //     }
    // }
}

template <class T> 
class MontageQueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MontageQueue<T>(gtc);
    }
};

/* Specialization for strings */
#include <string>
#include "PString.hpp"
template <>
class MontageQueue<std::string>::Payload : public pds::PBlk{
    GENERATE_FIELD(pds::PString<TESTS_VAL_SIZE>, val, Payload);
    GENERATE_FIELD(uint64_t, sn, Payload);

public:
    Payload(std::string v, uint64_t n) : m_val(this, v), m_sn(n){}
    Payload(const Payload& oth) : pds::PBlk(oth), m_val(this, oth.m_val), m_sn(oth.m_sn){}
    void persist(){}
};

#endif

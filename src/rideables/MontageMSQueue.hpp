#ifndef MONTAGE_MS_QUEUE
#define MONTAGE_MS_QUEUE

#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RQueue.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"
#include "Recoverable.hpp"
#include "persist_struct_api.hpp"
#include "DCSS.hpp"

using namespace pds;

template<typename T>
class MontageMSQueue : public RQueue<T>, Recoverable{
public:
    class Payload : public PBlk{
        GENERATE_FIELD(T, val, Payload);
        GENERATE_FIELD(uint64_t, sn, Payload); 
    public:
        Payload(): PBlk(){}
        Payload(T v): PBlk(), m_val(v), m_sn(0){}
        // Payload(const Payload& oth): PBlk(oth), m_sn(0), m_val(oth.m_val){}
        void persist(){}
    };

private:
    struct Node{
        atomic_nbptr_t<Node*> next;
        Payload* payload;

        Node(): next(nullptr), payload(nullptr){}; 
        Node(T v): next(nullptr), payload(PNEW(Payload, v)){
            assert(epochs[_tid].ui == NULL_EPOCH);
        };

        void set_sn(uint64_t s){
            assert(payload!=nullptr && "payload shouldn't be null");
            payload->set_unsafe_sn(s);
        }
        ~Node(){ 
            PRECLAIM(payload);
        }
    };

public:
    std::atomic<uint64_t> global_sn;

private:
    // dequeue pops node from head
    atomic_nbptr_t<Node*> head;
    // enqueue pushes node to tail
    std::atomic<Node*> tail;
    RCUTracker<Node> tracker;

public:
    MontageMSQueue(GlobalTestConfig* gtc): 
        Recoverable(gtc), global_sn(0), head(nullptr), tail(nullptr), 
        tracker(gtc->task_num, 100, 1000, true){

        Node* dummy = new Node();
        head.store(dummy);
        tail.store(dummy);
    }

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    int recover(bool simulated){
        errexit("recover of MontageMSQueue not implemented.");
        return 0;
    }

    ~MontageMSQueue(){};

    void enqueue(T val, int tid);
    optional<T> dequeue(int tid);
};

template<typename T>
void MontageMSQueue<T>::enqueue(T v, int tid){
    Node* new_node = new Node(v);
    Node* cur_tail = nullptr;
    tracker.start_op(tid);
    while(true){
        // Node* cur_head = head.load();
        cur_tail = tail.load();
        uint64_t s = global_sn.fetch_add(1);
        nbptr_t next = cur_tail->next.load();
        if(cur_tail == tail.load()){
            if(next.get_val<Node*>() == nullptr) {
                // directly set m_sn and BEGIN_OP will flush it
                new_node->set_sn(s);
                BEGIN_OP();
                new_node->payload->set_epoch(esys->epochs[EpochSys::tid].ui);
                /* set_sn must happen before PDELETE of payload since it's 
                 * before linearization point.
                 * Also, this must set sn in place since we still remain in
                 * the same epoch.
                 */
                // new_node->set_sn(s);
                if((cur_tail->next).CAS_verify(next, new_node)){
                    esys->register_alloc_pblk(new_node->payload, esys->epochs[EpochSys::tid].ui);
                    END_OP;
                    break;
                }
                ABORT_OP;
            } else {
                tail.compare_exchange_strong(cur_tail, next.get_val<Node*>()); // try to swing tail to next node
            }
        }
    }
    tail.compare_exchange_strong(cur_tail, new_node); // try to swing tail to inserted node
    tracker.end_op(tid);
}

template<typename T>
optional<T> MontageMSQueue<T>::dequeue(int tid){
    optional<T> res = {};
    tracker.start_op(tid);
    while(true){
        nbptr_t cur_head = head.load();
        Node* cur_tail = tail.load();
        Node* next = cur_head.get_val<Node*>()->next.load_val();

        if(cur_head == head.load()){
            if(cur_head.get_val<Node*>() == cur_tail){
                // queue is empty
                if(next == nullptr) {
                    res.reset();
                    break;
                }
                tail.compare_exchange_strong(cur_tail, next); // tail is falling behind; try to update
            } else {
                BEGIN_OP();
                Payload* payload = next->payload;// get payload for PDELETE
                if(head.CAS_verify(cur_head, next)){
                    res = (T)payload->get_val();// old see new is impossible
                    PRETIRE(payload); // semantically we are removing next from queue
                    END_OP;
                    cur_head.get_val<Node*>()->payload = payload; // let payload have same lifetime as dummy node
                    tracker.retire(cur_head.get_val<Node*>(), tid);
                    break;
                }
                ABORT_OP;
            }
        }
    }
    tracker.end_op(tid);
    return res;
}

template <class T> 
class MontageMSQueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MontageMSQueue<T>(gtc);
    }
};

/* Specialization for strings */
#include <string>
#include "PString.hpp"
template <>
class MontageMSQueue<std::string>::Payload : public PBlk{
    GENERATE_FIELD(PString<TESTS_VAL_SIZE>, val, Payload);
    GENERATE_FIELD(uint64_t, sn, Payload); 

public:
    Payload(std::string v) : m_val(this, v), m_sn(0){}
    void persist(){}
};

#endif
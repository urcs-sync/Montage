#ifndef MS_QUEUE
#define MS_QUEUE

#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RQueue.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"

template<typename T>
class MSQueue : public RQueue<T>{
private:
    struct Node{
        std::atomic<Node*> next;
        T val;

        Node(): next(nullptr), val(){}; 
        Node(T v): next(nullptr), val(v){};
        ~Node(){ }
    };

    // dequeue pops node from head
    std::atomic<Node*> head;
    // enqueue pushes node to tail
    std::atomic<Node*> tail;
    RCUTracker tracker;

public:
    MSQueue(int task_num): 
        head(nullptr), tail(nullptr), 
        tracker(task_num, 100, 1000, true){
        Node* dummy = new Node();
        head.store(dummy);
        tail.store(dummy);
    }

    ~MSQueue(){};

    void enqueue(T val, int tid);
    optional<T> dequeue(int tid);
};

template<typename T>
void MSQueue<T>::enqueue(T v, int tid){
    Node* new_node = new Node(v);
    Node* cur_tail = nullptr;
    tracker.start_op(tid);
    while(true){
        // Node* cur_head = head.load();
        cur_tail = tail.load();
        Node* next = cur_tail->next.load();
        if(cur_tail == tail.load()){
            if(next == nullptr) {
                if((cur_tail->next).compare_exchange_strong(next, new_node)){
                    break;
                }
            } else {
                tail.compare_exchange_strong(cur_tail, next); // try to swing tail to next node
            }
        }
    }
    tail.compare_exchange_strong(cur_tail, new_node); // try to swing tail to inserted node
    tracker.end_op(tid);
}

template<typename T>
optional<T> MSQueue<T>::dequeue(int tid){
    optional<T> res = {};
    tracker.start_op(tid);
    while(true){
        Node* cur_head = head.load();
        Node* cur_tail = tail.load();
        Node* next = cur_head->next.load();

        if(cur_head == head.load()){
            if(cur_head == cur_tail){
                // queue is empty
                if(next == nullptr) {
                    res.reset();
                    break;
                }
                tail.compare_exchange_strong(cur_tail, next); // tail is falling behind; try to update
            } else {
                res = next->val;
                if(head.compare_exchange_strong(cur_head, next)){
                    tracker.retire(cur_head, tid);
                    break;
                }
            }
        }
    }
    tracker.end_op(tid);
    return res;
}

template <class T> 
class MSQueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MSQueue<T>(gtc->task_num);
    }
};

#endif
#ifndef QUEUE_HPP
#define QUEUE_HPP

// transient queue
#include "TestConfig.hpp"
#include "RQueue.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include <mutex>

template<typename T>
class DRAMQueue : public RQueue<T>{
private:
    struct Node{
        T val;
        // Transient-to-transient pointers
        Node* next = nullptr;
        Node() : val("") {}
        Node(T v) : val(v){ }
        inline T get_val(){
            return val;
        }
        inline void set_val(T v){
            val = v;
        }
        ~Node(){ }
    };
    // dequeue pops node from head
    Node* head;
    // enqueue pushes node to tail
    Node* tail;
    std::mutex lock;
public:
    DRAMQueue(GlobalTestConfig* gtc) : head(nullptr), tail(nullptr){ };

    void enqueue(T val, int tid){
        Node* new_node = new Node(val);
        std::lock_guard<std::mutex> lk(lock);
        if(tail == nullptr) {
            head = tail = new_node;
            return;
        }
        tail->next = new_node;
        tail = new_node;
    }
    optional<T> dequeue(int tid){
        optional<T> res = {};
        lock.lock();
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
    }
};

#include "PString.hpp"
class NVMQueue : public RQueue<string>{
private:
    struct Node{
        char val[TESTS_VAL_SIZE];
        // Transient-to-transient pointers
        Node* next = nullptr;
        Node() {}
        Node(const std::string& v){
            memcpy(val, v.data(), v.size());
        }
        inline string get_val(){
            return string(val);
        }
        inline void set_val(const std::string& v){
            memcpy(val, v.data(), v.size());
        }
        ~Node(){ }
    };
    // dequeue pops node from head
    Node* head;
    // enqueue pushes node to tail
    Node* tail;
    std::mutex lock;
public:
    NVMQueue(GlobalTestConfig* gtc) : head(nullptr), tail(nullptr){ };

    void enqueue(string val, int tid){
        Node* new_node = new Node(val);
        std::lock_guard<std::mutex> lk(lock);
        if(tail == nullptr) {
            head = tail = new_node;
            return;
        }
        tail->next = new_node;
        tail = new_node;
    }
    optional<string> dequeue(int tid){
        optional<string> res = {};
        lock.lock();
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
    }
};

#ifndef PLACE_DRAM
  #define PLACE_DRAM 1
  #define PLACE_NVM 2
#endif
template <class T, int place = PLACE_DRAM> 
class QueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        if(place == PLACE_DRAM)
            return new DRAMQueue<T>(gtc);
        else
            return new NVMQueue(gtc);
    }
};

#endif
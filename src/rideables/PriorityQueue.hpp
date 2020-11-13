#ifndef PRIORITY_QUEUE
#define PRIORITY_QUEUE

#include <iostream>
#include <atomic>
#include <algorithm>
#include <memory>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"
#include "Recoverable.hpp"
#include "HeapQueue.hpp"

using namespace pds;
//Wentao: TODO to fix later
template<typename K, typename V>
class PriorityQueue : public HeapQueue<K,V>, public Recoverable{
public: 
  class Payload : public PBlk{
    GENERATE_FIELD(K, key, Payload);
    GENERATE_FIELD(V, val, Payload);
    GENERATE_FIELD(uint64_t, sn, Payload);
  public:
    Payload(){}
    Payload(K k, V v):m_key(k),  m_val(v), m_sn(0){}
    void persist(){}
  };

private:
  struct Node{
    PriorityQueue* ds;
    K key;
    Node* next;
    Payload* payload;

    Node():key(0), next(nullptr), payload(nullptr){};
    Node(PriorityQueue* ds_, K k, V val):
      ds(ds_), key(k), next(nullptr), payload(ds->pnew<Payload>(k, val)){};

    V get_val(){
      assert(payload != nullptr && "payload shouldn't be null");
      return (V)payload->get_val(ds);
    }

    void set_sn(uint64_t s){
      assert(payload != nullptr && "payload shouldn't be null");
      payload->set_sn(ds,s);
    }

    ~Node(){
      ds->pdelete(payload);
    }
  };

public:
  std::atomic<uint64_t> global_sn;

private:
  std::mutex mtx;
  Node* head;

public:
  PriorityQueue(int task_num): global_sn(0){
    head = new Node();
  }
  ~PriorityQueue(){};

  void enqueue(K key, V val, int tid);
  optional<V> dequeue(int tid);
};

template<typename K, typename V>
void PriorityQueue<K,V>::enqueue(K key, V val, int tid){
  Node* new_node = new Node(this, key, val);
  std::unique_lock<std::mutex> lock(mtx);
  if(head->next == nullptr){
    head->next = new_node;
  }else{
    if(key >= head->next->key){
      new_node->next = head->next;
      head->next = new_node;
    }else{
      Node* tmp = head->next;
      while(tmp->next != nullptr && key < tmp->next->key){
        tmp = tmp->next;
      }
      new_node->next = tmp->next;
      tmp->next = new_node;
    }
  }
  uint64_t s = global_sn.fetch_add(1);
  begin_op();
  new_node->set_sn(s);
  end_op();
}

template<typename K, typename V>
optional<V> PriorityQueue<K,V>::dequeue(int tid){
  std::unique_lock<std::mutex> lock(mtx);
  optional<V> res = {};
  if(head->next == nullptr){
    res.reset();
  }else{
    Node* target = head->next;
    head->next = target->next;
    begin_op();
    res = (V)target->payload->get_val();
    delete(target);
    end_op();
  }
  return res;
}

template <class T> 
class PriorityQueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new PriorityQueue<T,T>(gtc->task_num);
    }
};


#include <string>
#include "PString.hpp"
template<>
class PriorityQueue<std::string, std::string>::Payload : public PBlk{
  GENERATE_FIELD(PString<TESTS_KEY_SIZE>, key, Payload);
  GENERATE_FIELD(PString<TESTS_VAL_SIZE>, val, Payload);
  GENERATE_FIELD(uint64_t, sn, Payload);

public:
  Payload(std::string k, std::string v):m_key(this, k),  m_val(this, v), m_sn(0){}
  void persist(){}
};

#endif
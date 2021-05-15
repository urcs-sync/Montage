#ifndef PTREIBERSTACK_HPP
#define PTREIBERSTACK_HPP

#include <stdio.h>
#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RStack.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"

using namespace std;
using namespace persist_func;

template <typename T>
class PTreiberStack : public RStack<T>
{

private:
    struct StackNode : public Persistent
    {
        T data;
        StackNode *next;
        StackNode(T d, StackNode* n) : data(d), next(n){
            clwb_range_nofence(this, sizeof(StackNode));
        };
        inline T get_val(){ return data;}
        inline void set_val(T d){ 
            data = d; 
            clwb_range_nofence(data, sizeof(T));
        }
    };

    atomic<StackNode *> top;
    RCUTracker<StackNode> tracker;

public:
    PTreiberStack(int task_num) : top(nullptr), tracker(task_num, 100, 1000, true) {
        Persistent::init();
    }
    ~PTreiberStack(){
        Persistent::finalize();
    }
    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Persistent::init_thread(gtc, ltc);
    }
    void push(T data, int tid);
    optional<T> pop(int tid);
    optional<T> peek(int tid);
    bool is_empty();
};

template <typename T>
void PTreiberStack<T>::push(T data, int tid)
{
    tracker.start_op(tid);

    StackNode *new_node = new StackNode(data,nullptr);

    StackNode *old_node;

    do
    {
        old_node = top.load();
        clwb(&top);
        new_node->next = old_node;
        clwb(&(new_node->next));
        sfence();
    } while (!top.compare_exchange_weak(old_node, new_node));
    clwb(&top);
    sfence();

    tracker.end_op(tid);
}

template <typename T>
optional<T> PTreiberStack<T>::pop(int tid)
{
    tracker.start_op(tid);
    StackNode *new_node;
    StackNode *old_node;
    optional<T> res = {};
    do
    {
        old_node = top.load();
        clwb(&top);
        sfence();
        if (old_node == nullptr)
        {
            tracker.end_op(tid);
            return res;
        }
        new_node = old_node->next;
    } while (!top.compare_exchange_weak(old_node, new_node));
    clwb(&top);
    sfence();
    res = old_node->data;
    tracker.retire(old_node, tid);
    tracker.end_op(tid);
    return res;
}
// not persisted
template <typename T>
optional<T> PTreiberStack<T>::peek(int tid)
{
    tracker.start_op(tid);
    optional<T> res = {};
    StackNode *top_node;
    top_node = top.load();
    if (top_node == nullptr){
        tracker.end_op(tid);
        return res;
    }
    tracker.end_op(tid);
    res = top_node->data;
    return res;
}

template <typename T>
bool PTreiberStack<T>::is_empty()
{
    return top.load() == NULL;
}

template <class T>
class PTreiberStackFactory : public RideableFactory
{
    Rideable *build(GlobalTestConfig *gtc)
    {
        return new PTreiberStack<T>(gtc->task_num);
    }
};

template <>
struct PTreiberStack<std::string>::StackNode : public Persistent{
    char data[TESTS_VAL_SIZE];
    StackNode *next;
    StackNode(std::string d, StackNode* n) : next(n){
        assert(d.size()<=TESTS_VAL_SIZE);
        memcpy(data, d.data(), d.size());
        clwb_range_nofence(this, sizeof(StackNode));
    }
    inline std::string get_val(){ 
        return std::string(data,TESTS_VAL_SIZE);
    }
    inline void set_val(std::string d){ 
        assert(d.size()<=TESTS_VAL_SIZE);
        memcpy(data, d.data(), d.size());
        clwb_range_nofence(data, TESTS_VAL_SIZE);
    }
};

#endif

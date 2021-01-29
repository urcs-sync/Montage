#ifndef TREIBERSTACK_HPP
#define TREIBERSTACK_HPP

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

template <typename T>
class TreiberStack : public RStack<T>
{

private:
    struct StackNode
    {
        T data;
        StackNode *next;
        StackNode(T d, StackNode* n) : data(d), next(n){};
        inline T get_val(){ return data;}
        inline void set_val(T d){ data = d; }
    };

    atomic<StackNode *> top;
    RCUTracker<StackNode> tracker;

public:
    TreiberStack(int task_num) : top(nullptr), tracker(task_num, 100, 1000, true) {}
    ~TreiberStack(){};
    void push(T data, int tid);
    optional<T> pop(int tid);
    optional<T> peek(int tid);
    bool is_empty();
};

template <typename T>
void TreiberStack<T>::push(T data, int tid)
{
    tracker.start_op(tid);

    StackNode *new_node = new StackNode(data,nullptr);

    StackNode *old_node;

    do
    {
        old_node = top.load();
        new_node->next = old_node;
    } while (!top.compare_exchange_weak(old_node, new_node));

    tracker.end_op(tid);
}

template <typename T>
optional<T> TreiberStack<T>::pop(int tid)
{
    tracker.start_op(tid);
    StackNode *new_node;
    StackNode *old_node;
    optional<T> res = {};
    do
    {
        old_node = top.load();
        if (old_node == nullptr)
        {
            tracker.end_op(tid);
            return res;
        }
        new_node = old_node->next;
    } while (!top.compare_exchange_weak(old_node, new_node));
    res = old_node->data;
    tracker.retire(old_node, tid);
    tracker.end_op(tid);
    return res;
}
template <typename T>
optional<T> TreiberStack<T>::peek(int tid)
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
bool TreiberStack<T>::is_empty()
{
    return top.load() == NULL;
}

template <class T>
class TreiberStackFactory : public RideableFactory
{
    Rideable *build(GlobalTestConfig *gtc)
    {
        return new TreiberStack<T>(gtc->task_num);
    }
};

template <>
struct TreiberStack<std::string>::StackNode{
    char data[TESTS_VAL_SIZE];
    StackNode *next;
    StackNode(std::string d, StackNode* n) : next(n){
        assert(d.size()<=TESTS_VAL_SIZE);
        memcpy(data, d.data(), d.size());
    }
    inline std::string get_val(){ 
        return std::string(data,TESTS_VAL_SIZE);
    }
    inline void set_val(std::string d){ 
        assert(d.size()<=TESTS_VAL_SIZE);
        memcpy(data, d.data(), d.size()); 
    }
};

#endif
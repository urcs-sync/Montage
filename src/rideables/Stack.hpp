// #ifndef STACK_HPP
// #define STACK_HPP

#include <stdio.h>
#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RQueue.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"

using namespace std;

template <typename T>
class Stack
{

private:
    struct StackNode
    {
        T data;
        StackNode *next;
    };

    atomic<StackNode *> top;
    RCUTracker<StackNode> tracker;

public:
    Stack(int task_num) : top(nullptr), tracker(task_num, 100, 1000, true) {}
    ~Stack(){};
    void push(T data, int tid);
    optional<T> pop(int tid);
    optional<T> peek(int tid);
    bool is_empty();
};

template <typename T>
void Stack<T>::push(T data, int tid)
{
    tracker.start_op(tid);

    StackNode *new_node = new StackNode;
    new_node->data = data;
    new_node->next = NULL;

    StackNode *old_node;

    do
    {
        old_node = top.load();
        new_node->next = old_node;
    } while (!top.compare_exchange_weak(old_node, new_node));

    tracker.end_op(tid);
}

template <typename T>
optional<T> Stack<T>::pop(int tid)
{
    tracker.start_op(tid);
    StackNode *new_node;
    StackNode *old_node;
    optional<T> res = {};
    do
    {
        old_node = top.load();
        if (old_node == NULL)
        {
            tracker.end_op(tid);
            return res;
        }
        new_node = old_node->next;
    } while (!top.compare_exchange_weak(old_node, new_node));
    res = old_node->data;
    tracker.retire(old_node);
    tracker.end_op(tid);
    return res;
}
template <typename T>
optional<T> Stack<T>::peek(int tid)
{
    tracker.start_op(tid);
    optional<T> res = {};
    StackNode *top_node;
    top_node = top.load();
    tracker.end_op(tid);
    res = top_node->data;
    return res;

}

template <typename T>
bool Stack<T>::is_empty()
{
    return top.load() == NULL;
}

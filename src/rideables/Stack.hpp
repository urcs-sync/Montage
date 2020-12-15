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
        int data;
        StackNode *next;
    };

    atomic<StackNode *> top;
    RCUTracker<StackNode> tracker;

public:
    Stack(int task_num) : top(nullptr), tracker(task_num, 100, 1000, true) {}
};

template <typename T>
void Stack<T>::push(int data, int tid)
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
optional<T> MSQueue<T>::pop(int tid)
{
    tracker.start_op(tid);
    StackNode *new_node;
    StackNode *old_node;
    do
    {
        old_node = top.load();
        if (old_node == NULL)
        {
            tracker.end_op(tid);
            return "Error : popping an empty stack";
        }
        new_node = old_node->next;
    } while (!top.compare_exchange_weak(old_node, new_node));
    int data = old_node->data;
    tracker.retire(old_node);
    tracker.end_op(tid);
    return data;
}
optional<T> peek(int tid)
{
    tracker.start_op(tid);
    if (!is_empty())
    {
        StackNode *top_node;
        top_node = top.load();
        tracker.end_op(tid);
        return top_node->data;
    }
    else
    {
        //TODO : throw errro
        tracker.end_op(tid);
        return "Error : peeking an empty stack";
    }
}

bool is_empty()
{
    return top.load() == NULL;
}

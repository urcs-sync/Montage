// #ifndef STACK_HPP
// #define STACK_HPP

#include <stdio.h>
#include <iostream>
#include <atomic>

using namespace std;

class Stack
{
private:

    struct StackNode
    {

        int data;
        StackNode *next;
    };

    atomic<StackNode *> top;

public:

    Stack() : top(nullptr) {}

    void push(int data)
    {

        StackNode *new_node = new StackNode;
        new_node->data = data;
        new_node->next = NULL;

        StackNode *old_node;

        do
        {
            old_node = top.load();
            new_node->next = old_node;
        } while (!top.compare_exchange_weak(old_node, new_node));
    }

    int pop()
    {
        StackNode *new_node;
        StackNode *old_node;
        do
        {
            old_node = top.load();
            if (old_node == NULL)
            {
                return NULL;
            }
            new_node = old_node->next;
        } while (!top.compare_exchange_weak(old_node, new_node));
        return old_node->data;
    }
    int peek()
    {
        if (!is_empty())
        {
            StackNode *top_node;
            top_node = top.load();
            return top_node->data;
        }
        else
        {
            //TODO : throw errro
            return 55555;
        }
    }

    bool is_empty()
    {
        return top.load() == NULL;
    }
};
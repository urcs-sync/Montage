// #ifndef STACK_HPP
// #define STACK_HPP

#include <stdio.h>
#include<iostream>
#include <atomic>

using namespace std;

class Stack
{
    private:

    typedef struct StackNode
    {

        int data;
        StackNode *next;

    } * StackNodePtr;

    atomic<StackNodePtr> top;

    public:
    Stack()
    {
        top = NULL;
    }
    void push(int data){

        StackNodePtr new_node = new StackNode;
        new_node->data = data;
        new_node->next = NULL;

        StackNodePtr old_node;

        do {
            old_node = top;
            new_node->next = old_node;
        } while (!top.compare_exchange_strong(old_node, new_node));


    }
    int pop(){
        StackNodePtr new_node;
        StackNodePtr old_node;
        do {
            old_node = top;
            if (old_node == NULL){
                return NULL;
            }
            new_node = old_node->next;
        } while (!top.compare_exchange_strong(old_node, new_node));
        return old_node->data;
    }
    int peek(){
        if(!is_empty()){
            StackNodePtr top_node;
            top_node = top;
            return top_node->data;
        }else{
            //TODO : throw errro
            return 55555;
        }

    }

    bool is_empty(){
        return top == NULL;
    }
};
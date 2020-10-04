#ifndef MNE_QUEUE_P
#define MNE_QUEUE_P

#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RQueue.hpp"
#include <mutex>
#include <string>

#include "mnemosyne.h"
#include "mtm.h"
#include "pmalloc.h"
#define TM_SAFE __attribute__((transaction_safe))
#define PTx     __transaction_atomic
template<typename T>
class MneQueue : public RQueue<T>{
    TM_SAFE
    static void *
    txc_libc_memcpy (char *dest, const char *src, const int bytes)
    {
        const char *p;
        char *q;
        int count;

        {
            for(p = src, q = dest, count = 0; count < bytes; p++, q++, count++)
                *q = *p;
        }

        return dest;
    }
    TM_SAFE
    static int
    txc_libc_strlen (const char *src)
    {
        int count = 0;
        char x = *src;
        while(x != '\0') {
            ++count;
            ++src;
            x = *src;
        }
        return count;
    }
    class Node{
    public:
        char* val;
        Node* next;
        Node(const char* v, size_t v_size, Node* n=nullptr){
            PTx{
                val = (char*)pmalloc(v_size+1);
                txc_libc_memcpy(val, v, v_size);
                val[v_size]='\0';
                next = n;
            }
        }
        Node(){
        }
        ~Node(){
        }
        static void* operator new(std::size_t sz){
            void* ret;
            PTx{ret = pmalloc(sz);}
            return ret;
        }
        static void* operator new[](std::size_t sz){
            void* ret;
            PTx{ret = pmalloc(sz);}
            return ret;
        }
        static void operator delete(void* ptr, std::size_t sz){
            PTx{pfree(ptr);}
            return;
        }
        static void operator delete[](void* ptr, std::size_t sz){
            PTx{pfree(ptr);}
            return;
        }
    };
    // dequeue pops node from head
    Node* head;
    // enqueue pushes node to tail
    Node* tail;

public:
    MneQueue(int task_num): 
        head(nullptr), tail(nullptr){
    }

    ~MneQueue(){};

    // struct cstr_holder{
    //     char* cstr;
    //     cstr_holder(char* s):cstr(s){};
    //     ~cstr_holder(){free(cstr);}
    // };
    void enqueue(T val, int tid);
    optional<T> dequeue(int tid);
};

template<typename T>
void MneQueue<T>::enqueue(T val, int tid){
    Node* new_node = new Node(val.data(),val.size());
    PTx{
        if(tail == nullptr) {
            head = tail = new_node;
        } else {
            tail->next = new_node;
            tail = new_node;
        }
    }
    return;
}

template<typename T>
optional<T> MneQueue<T>::dequeue(int tid){
    // cstr_holder holder(_ret);
    optional<T> res = {};
    Node* tmp = nullptr;
    char* _ret = (char*)malloc(5000);
    bool exist = false;

    PTx{
        if(head != nullptr) {
            tmp = head;
            txc_libc_memcpy(_ret, tmp->val, txc_libc_strlen(tmp->val)+1);
            exist = true;
            head = head->next;
            if(head == nullptr) {
                tail = nullptr;
            }
        }
    }

    if(exist){
        res = std::string(_ret);
        delete(tmp);
        free(_ret);
    }
    return res;

}

template <class T> 
class MneQueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MneQueue<T>(gtc->task_num);
    }
};

#endif

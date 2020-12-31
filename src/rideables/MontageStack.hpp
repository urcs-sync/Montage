#ifndef MONTAGESTACK_HPP
#define MONTAGESTACK_HPP

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
class MontageStack : public RStack<T>, Recoverable
{

private:
    struct StackNode
    {
        StackNode *next;
        Payload *data;

        MontageStack *ds = nullptr;

        void set_sn(uint64_t s)
        {
            assert(data != nullptr && "payload shouldn't be null");
            data->set_unsafe_sn(ds, s);
        }
        StackNode(){};
        StackNode(T v) : next(nullptr), data(pnew<Payload>(v)){};
        ~StackNode()
        {
            if (data)
            {
                ds->preclaim(data);
            }
        }
    };

public:
    std::atomic<uint64_t> global_sn;

private:
    pds::atomic_lin_var<StackNode *> top;
    RCUTracker<StackNode> tracker;

public:
    class Payload : public pds::PBlk
    {
        GENERATE_FIELD(T, val, Payload);
        GENERATE_FIELD(uint64_t, sn, Payload);

    public:
        Payload() {}
        Payload(T v, uint64_t n) : m_val(v), m_sn(n) {}
        Payload(const Payload &oth) : PBlk(oth), m_sn(0), m_val(oth.m_val) {}
        void persist() {}
    };
    MontageStack(GlobalTestConfig* gtc) : Recoverable(gtc), global_sn(0), top(nullptr), tracker(gtc->task_num, 100, 1000, true) {}
    ~MontageStack(){};
    
    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    int recover(bool simulated){
        errexit("recover of MontageStack not implemented.");
        return 0;
    }
    void push(T data, int tid);
    optional<T> pop(int tid);
    optional<T> peek(int tid);
    bool is_empty();
};

template <typename T>
void MontageStack<T>::push(T data, int tid)
{
    tracker.start_op(tid);

    StackNode *new_node = new StackNode(data);

    StackNode *old_node;

    while(true)
    {
        old_node = top.load();
        uint64_t s = global_sn.fetch_add(1);
        new_node->next = old_node;
        new_node->set_sn(s);
        begin_op();
        if(top.CAS_verify(this, old_node, new_node)){
            end_op();
            break;
        }
        abort_op();

    };

    tracker.end_op(tid);
}

template <typename T>
optional<T> MontageStack<T>::pop(int tid)
{
    tracker.start_op(tid);
    StackNode *new_node;
    StackNode *old_node;
    optional<T> res = {};
    while(true)
    {
        old_node = top.load();
        if (old_node == NULL)
        {
            tracker.end_op(tid);
            return res;
        }
        begin_op();
        new_node = old_node->next;
        if (top.CAS_verify(this, old_node, new_node))){
            res = (T)old_node->data->get_val(this);// old see new is impossible
            pretire(old_node->data); 
            end_op();
            break;
        }
        abort_op();
    };
    tracker.retire(old_node, tid);
    tracker.end_op(tid);
    return res;
}
template <typename T>
optional<T> MontageStack<T>::peek(int tid)
{
    tracker.start_op(tid);
    optional<T> res = {};
    StackNode *top_node;
    top_node = top.load();
    tracker.end_op(tid);
    auto data = top_node->data;
    res = (T)data->get_val(this);
    return res;
}

template <typename T>
bool MontageStack<T>::is_empty()
{
    return top.load() == NULL;
}

template <class T>
class MontageStackFactory : public RideableFactory
{
    Rideable *build(GlobalTestConfig *gtc)
    {
        return new MontageStack<T>(gtc->task_num);
    }
};

#endif
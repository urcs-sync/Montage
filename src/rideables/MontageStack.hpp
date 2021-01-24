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
public:
    class Payload : public pds::PBlk{
        GENERATE_FIELD(T, val, Payload);
        GENERATE_FIELD(uint64_t, sn, Payload); 
    public:
        Payload(): pds::PBlk(){}
        Payload(T v): pds::PBlk(), m_val(v), m_sn(0){}
        Payload(const Payload& oth): pds::PBlk(oth), m_sn(0), m_val(oth.m_val){}
        void persist(){}
    };

private:
    struct StackNode{
        MontageStack *ds = nullptr;
        pds::atomic_lin_var<StackNode*> next;
        Payload *payload;

        StackNode(): next(nullptr), payload(nullptr){}
        StackNode(MontageStack* ds_): ds(ds_), next(nullptr), payload(nullptr){}
        StackNode(MontageStack* ds_, T v): ds(ds_), next(nullptr), payload(ds_->pnew<Payload>(v)){}

        void set_sn(uint64_t s)
        {
            assert(payload != nullptr && "payload shouldn't be null");
            payload->set_unsafe_sn(ds, s);
        }
        ~StackNode(){
            if (payload)
            {
                ds->preclaim(payload);
            }
        }
    };

public:
    std::atomic<uint64_t> global_sn;

private:
    pds::atomic_lin_var<StackNode*> top;
    RCUTracker<StackNode> tracker;

public:

    MontageStack(GlobalTestConfig* gtc) : Recoverable(gtc), global_sn(0), top(nullptr), tracker(gtc->task_num, 100, 1000, true) {
        StackNode* dummy = new StackNode(this);
        top.store(dummy);
    }
    
    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }

    int recover(bool simulated){
        errexit("recover of MontageStack not implemented.");
        return 0;
    }
    ~MontageStack(){};

    void push(T v, int tid);
    optional<T> pop(int tid);
    optional<T> peek(int tid);
    bool is_empty();
};

template <typename T>
void MontageStack<T>::push(T v, int tid){
    StackNode *new_node = new StackNode(this,v);
    tracker.start_op(tid);

    while(true)
    {
        uint64_t s = global_sn.fetch_add(1);
        pds::lin_var old_node = top.load(this);
        new_node->next.store(old_node);
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
    optional<T> res = {};
    tracker.start_op(tid);
    
    while(true)
    {
        pds::lin_var old_node = top.load(this);
        if (is_empty())
        {
            tracker.end_op(tid);
            return res;
        }
        begin_op();
        StackNode* new_node = old_node.get_val<StackNode*>()->next.load_val(this);
        if (top.CAS_verify(this, old_node, new_node)){
            auto payload = old_node.get_val<StackNode*>()->payload;
            res = (T)payload->get_val(this);// old see new is impossible
            pretire(payload); 
            end_op();
            tracker.retire(old_node.get_val<StackNode*>(), tid);
            break;
        }
        abort_op();
    };
    tracker.end_op(tid);
    return res;
}
template <typename T>
optional<T> MontageStack<T>::peek(int tid)
{
    tracker.start_op(tid);
    optional<T> res = {};
    if(!is_empty()){

        StackNode *top_node;
        top_node = top.load(this);
        tracker.end_op(tid);
        auto payload = top_node->payload;
        res = (T)payload->get_val(this);

    }

    return res;
}

template <typename T>
bool MontageStack<T>::is_empty()
{
    return top.load_val(this) == nullptr;
}

template <class T>
class MontageStackFactory : public RideableFactory
{
    Rideable *build(GlobalTestConfig *gtc)
    {
        return new MontageStack<T>(gtc);
    }
};

#endif
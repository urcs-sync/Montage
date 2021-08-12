#ifndef PERSIST_UTILS_HPP
#define PERSIST_UTILS_HPP

#include "common_macros.hpp"
#include "ConcurrentPrimitives.hpp"
#include "HarnessUtils.hpp"
#include <atomic>
#include <vector>
#include <unordered_set>
#include <functional>

namespace pds{
    struct pair{
        void* first;
        size_t second;
        pair(void* f, size_t s): first(f), second(s){}
        pair(){}
        friend bool operator != (const pair& x, const pair& y){
            return (x.first != y.first || x.second != y.second);
        }
        friend bool operator == (const pair& x, const pair& y){
            return (x.first == y.first && x.second == y.second);
        }
    };
    // template<typename A, typename B>
    // struct padded_pair{
    //     A first;
    //     B second;
    //     padded_pair(A f, B s): first(f), second(s){}
    //     padded_pair(const pair<A,B>& p): first(p.first), second(p.second){}
    //     padded_pair(){}
    //     operator pair<A,B>() const {
    //         return pair<A,B>(first, second);
    //     }
    //     friend bool operator != (const padded_pair<A,B>& x, const pair<A,B>& y){
    //         return (x.first != y.first || x.second != y.second);
    //     }
    //     friend bool operator == (const padded_pair<A,B>& x, const pair<A,B>& y){
    //         return (x.first == y.first && x.second == y.second);
    //     }
    // }__attribute__((aligned(CACHE_LINE_SIZE)));
    struct padded_pair{
        // std::atomic<pair<A,B>> ui;
        pair ui;
        padded_pair(void* a, size_t b): ui({a,b}){}
    }__attribute__((aligned(CACHE_LINE_SIZE)));
};

class UIDGenerator{
    padded<uint64_t>* curr_ids = nullptr;
public:
    UIDGenerator(){}
    UIDGenerator(uint64_t task_num){
        init(task_num);
    }
    ~UIDGenerator(){
        if (curr_ids){
            delete curr_ids;
        }
    }
    void init(uint64_t task_num){
        uint64_t buf = task_num-1;
        int shift = 64;
        uint64_t max = 1;
        for (; buf != 0; buf >>= 1){
            shift--;
            max <<= 1;
        }
        if (!curr_ids){
            curr_ids = new padded<uint64_t>[max];
        }
        for (uint64_t i = 0; i < max; i++){
            curr_ids[i].ui = i << shift;
        }
    }
    uint64_t get_id(int tid){
        return curr_ids[tid].ui++;
    }
};

// A single-threaded circular buffer that grows exponentially
// when populated and never shrinks (for now).
// The buffer always allocates new spaces in chunks: the key
// idea is trying to allocate nodes in chunks to get
// better cache locality and lower the seeking overhead.
#define CIRCBUFFER_DEF_CAP 64
template<typename T>
class CircBuffer{
    struct Node{
        T payload;
        Node* next = nullptr;
    };
    // capacity
    int cap = CIRCBUFFER_DEF_CAP;
    // padding head/tail pointers: performs better when producer and consumer are on different threads.
    // head pointer, where things got popped off
    padded<Node*> head;
    // tail pointer, where things got pushed in
    padded<Node*> tail;
    // vector of allocated chunks. for destruction.
    std::vector<Node*> chunks;
    // find the next node of node p.
    Node* next_node(Node* p){
        // next pointer points to something non-trivial
        if (p->next){
            return p->next;
        } else {
            // next is null, go to the immediately next block.
            return ++p;
        }
    }
public:
    // constructor. allocate $cap Nodes and wrap the pointer around.
    CircBuffer(){
        head.ui = new Node[cap];
        head.ui[cap-1].next = head.ui;
        tail.ui = head.ui;
    }
    // destructor.
    ~CircBuffer(){
        for (auto itr = chunks.begin(); itr != chunks.end(); itr++){
            delete *itr;
        }
    }
    // push x into tail and move tail forward.
    void push(T x){
        // push x into tail
        tail.ui->payload = x;
        Node* next_tail = next_node(tail.ui);
        // if the buffer is full
        if(next_tail == head.ui){
            // double the capacity by allocating a new chunk
            // note that if we're in a chunk, it will be unfortunately broken into half.
            tail.ui->next = new Node[cap];
            chunks.push_back(tail.ui->next);
            // point the next pointer in newly created last block to the head
            tail.ui->next[cap-1].next = head.ui;
            // double the capacity
            cap <<= 1;
            // move the tail
            tail.ui = tail.ui->next;
        } else {
            tail.ui = next_tail;
        }
    }
    // try to pop the head into x.
    bool try_pop(T& x){
        if (head.ui == tail.ui){
            // empty.
            return false;
        } else {
            // not empty.
            x = head.ui->payload;
            head.ui = next_node(head.ui);
            return true;
        }
    }
    // try to pop the head before calling func.
    bool try_pop(const std::function<void(T& x)>& func){
        if (head.ui == tail.ui){
            // empty.
            return false;
        } else {
            // not empty.
            func(head.ui->payload);
            head.ui = next_node(head.ui);
            return true;
        }
    }
    void clear(){
        head.ui = tail.ui;
    }
}__attribute__((aligned(CACHE_LINE_SIZE)));

// a group of per-thread circular buffer
// NOTE: The container is NOT thread safe.
template<typename T>
class PerThreadCircBuffer{
    // count of threads (and buffers)
    int count;
    padded<CircBuffer<T>*>* buffers;
public:
    PerThreadCircBuffer(int task_num){
        count = task_num;
        // init the buffers.
        buffers = new padded<CircBuffer<T>*>[count];
        for (int i = 0; i < count; i++){
            buffers[i].ui = new CircBuffer<T>();
        }
    }
    ~PerThreadCircBuffer(){
        for (int i = 0; i < count; i++){
            delete buffers[i].ui;
        }
        delete buffers;
    }
    void push(T x, int tid){
        buffers[tid].ui->push(x);
    }
    void pop_all(const std::function<void(T& x)>& func){
        // std::cout<<"pop_all called"<<std::endl;
        for (int i = 0; i < count; i++){
            while(buffers[i].ui->try_pop(func)){}
        }
    }
    void pop_all_local(const std::function<void(T& x)>& func, int tid){
        while(buffers[tid].ui->try_pop(func)){}
    }
    bool try_pop_local(const std::function<void(T& x)>& func, int tid){
        return buffers[tid].ui->try_pop(func);
    }
    void clear(){
        for (int i = 0; i < count; i++){
            buffers[i].ui->clear();
        }
    }
};

// a fixed-sized circular buffer.
// single producer, multiple consumer.
// concurrent consumers may pop the same entry multiple times.
template<typename T>
class FixedCircBuffer{
    size_t cap;
    paddedAtomic<size_t> pushed;
    paddedAtomic<size_t> popped;
    padded<T>* payloads = nullptr;
public:
    FixedCircBuffer(size_t _cap) : 
            cap(_cap){
        payloads = new padded<T>[cap];
        pushed.ui = 0;
        popped.ui = 0;
    }
    ~FixedCircBuffer(){
        delete(payloads);
    }
    void push(T x, const std::function<void(T& x)>& func){
        size_t curr_pushed = pushed.ui.load(std::memory_order_acquire);
        size_t curr_popped = popped.ui.load(std::memory_order_acquire);
        assert(curr_pushed <= curr_popped + cap);
        if (curr_pushed == curr_popped + cap){
            // full, consume one entry.
            func(payloads[curr_popped%cap].ui);
            // try to pop the consumed entry, failiure is harmless.
            popped.ui.compare_exchange_strong(curr_popped, curr_popped+1, std::memory_order_acq_rel);
        }
        // push x
        payloads[curr_pushed%cap].ui = x;
        // advance pushed counter.
        pushed.ui.store(curr_pushed+1, std::memory_order_release);
    }
    // bool try_push(T x){
    //     size_t curr_pushed = pushed.ui.load(std::memory_order_acquire);
    //     size_t curr_popped = popped.ui.load(std::memory_order_acquire);
    //     assert(curr_pushed <= curr_popped + cap);
    //     if (curr_pushed == curr_popped + cap){
    //         // full, return false
    //         return false;
    //     } else {
    //         // push x
    //         payloads[curr_pushed%cap].ui = x;
    //         // advance pushed counter.
    //         pushed.ui.store(curr_pushed+1, std::memory_order_release);
    //         return true;
    //     }
    // }
    bool try_pop(const std::function<void(T& x)>& func){
        size_t curr_popped = popped.ui.load(std::memory_order_acquire);
        size_t curr_pushed = pushed.ui.load(std::memory_order_acquire);
        assert(curr_popped <= curr_pushed);
        if (curr_popped == curr_pushed){
            // empty
            return false;
        }
        // consume the to-be-popped entry.
        func(payloads[curr_popped%cap].ui);
        // try to pop the next unpopped entry
        while(!popped.ui.compare_exchange_strong(curr_popped, curr_popped+1, std::memory_order_acq_rel)){
            curr_pushed = pushed.ui.load(std::memory_order_acquire);
            assert(curr_popped <= curr_pushed);
            if (curr_popped == curr_pushed){
                // empty
                return false;
            }
        }
        // successfully popped.
        return true;
    }
    bool try_pop(T& x){
        size_t curr_popped = popped.ui.load(std::memory_order_acquire);
        size_t curr_pushed = pushed.ui.load(std::memory_order_acquire);
        assert(curr_popped <= curr_pushed);
        if (curr_popped == pushed.ui.load(std::memory_order_acquire)){
            // empty
            return false;
        }
        // consume the to-be-popped entry.
        x = payloads[curr_popped%cap].ui;
        // try to pop the next unpopped entry
        while(!popped.ui.compare_exchange_strong(curr_popped, curr_popped+1, std::memory_order_acq_rel)){
            curr_pushed = pushed.ui.load(std::memory_order_acquire);
            assert(curr_popped <= curr_pushed);
            if (curr_popped == curr_pushed){
                // empty
                return false;
            }
        }
        // successfully popped.
        return true;
    }
    void pop_all(const std::function<void(T& x)>& func){
        while (true){
            size_t curr_popped = popped.ui.load(std::memory_order_acquire);
            size_t curr_pushed = pushed.ui.load(std::memory_order_acquire);
            assert(curr_popped <= curr_pushed);
            if (curr_popped == curr_pushed){
                // empty
                return;
            }
            // consume all entries.
            size_t i = curr_popped % cap;
            size_t end = (i + (curr_pushed - curr_popped)) % cap;
            if (end <= i){ // wrap around.
                while(i < cap){
                    func(payloads[i].ui);
                    i++;
                }
                i = 0;
            }
            while(i < end){
                func(payloads[i].ui);
                i++;
            }
            // try to CAS the container to empty. If faild, try over.
            if (popped.ui.compare_exchange_strong(curr_popped, curr_pushed, std::memory_order_acq_rel)){
                return;
            }
        }
    }
    void clear(){
        pushed.ui = 0;
        popped.ui = 0;
    }
}__attribute__((aligned(CACHE_LINE_SIZE)));

// Single-producer multiple-consumer fixed-sized hashmap
// An entry might be consumed by multiple consumers.
class FixedHashSet{
    int cap;
    // I could never get it to compile with one star,
    // because for non-trivial atomic type, default constructor is deleted.
    paddedAtomic<void*>* payloads = nullptr;
    std::atomic<int> last_pop;
public:
    FixedHashSet(int cap_) : cap(cap_){
        payloads = new paddedAtomic<void*>[cap];
        for (int i = 0; i < cap; i++){
            payloads[i] = nullptr;
        }
        last_pop = 0;
    }
    ~FixedHashSet(){
        delete payloads;
    }
    void push(void* x, const std::function<void(void*& x)>& func){
        auto idx = (((uint64_t)x) >> 6) % cap;
        auto exp = payloads[idx].ui.load();
        if (exp == x){
            return;
        }
        if (exp != nullptr){
            func(exp);
        }
        // we can simply store new value here rather than CAS, because
        // we can only overwrite either exp or (nullptr, 0).
        // (actually the latter case will only happen with nbMontage, and
        // x's operation must have been aborted.)
        payloads[idx].ui.store(x);
    }
    bool try_pop(const std::function<void(void*& x)>& func){
        // pop a random bucket (actually the next bucket of the previous one)
        auto idx = last_pop.fetch_add(1)%cap;
        auto exp = payloads[idx].ui.load();
        if (exp == nullptr){
            // returning false doesn't mean the container is empty.
            return false;
        }
        func(exp);
        payloads[idx].ui.compare_exchange_strong(exp, nullptr);
        return true;
    }
    bool try_pop(void*& x){
        errexit("FixedHashSet::try_pop(pair& x) not implemented.");
        return false;
    }
    void pop_all(const std::function<void(void*& x)>& func){
        for (int i = 0; i < cap; i++){
            auto exp = payloads[i].ui.load();
            if (exp == nullptr){
                continue;
            }
            func(exp);
            // we need to use CAS rather than a store here because we may get preempted
            // here and wake up several epochs later.
            // but, there won't be any correctness issues if we don't overwrite
            // with 0; we're only doing this to prevent redundant persisting.

            // payloads[i]->compare_exchange_strong(exp, pds::pair(nullptr));
        }
    }
    void clear(){
        for (int i = 0; i < cap; i++){
            payloads[i].ui.store(nullptr);
        }
    }
};

// a group of per-thread circular buffer
// NOTE: this is designed for single-consumer pattern only. The container is NOT thread safe.
template<typename T>
class PerThreadVector{
    // count of threads (and buffers)
    int count;
    padded<std::vector<T>*>* buffers;
public:
    PerThreadVector(int task_num){
        count = task_num;
        // init the buffers.
        buffers = new padded<std::vector<T>*>[count];
        for (int i = 0; i < count; i++){
            buffers[i].ui = new std::vector<T>();
        }
    }
    ~PerThreadVector(){
        for (int i = 0; i < count; i++){
            delete buffers[i].ui;
        }
        delete buffers;
    }
    void push(T x, int tid){
        buffers[tid].ui->push_back(x);
    }
    void pop_all(const std::function<void(T& x)>& func){
        for (int i = 0; i < count; i++){
            while(!buffers[i].ui->empty()){
                func(buffers[i].ui->back());
                buffers[i].ui->pop_back();
            }
        }
    }
    void pop_all_local(const std::function<void(T& x)>& func, int tid){
        while(!buffers[tid].ui->empty()){
            func(buffers[tid].ui->back());
            buffers[tid].ui->pop_back();
        }
    }
    bool try_pop_local(const std::function<void(T& x)>& func, int tid){
        if (buffers[tid].ui->empty()){
            return false;
        } else {
            func(buffers[tid].ui->back());
            buffers[tid].ui->pop_back();
            return true;
        }
        
    }
    void clear(){
        for (int i = 0; i < count; i++){
            buffers[i].ui->clear();
        }
    }
};


#endif
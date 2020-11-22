#ifndef PERTHREADCONTAINERS_HPP
#define PERTHREADCONTAINERS_HPP

#include <functional>

#include "ConcurrentPrimitives.hpp"
#include "persist_utils.hpp"

namespace pds{
///////////////////////////
// Concurrent Containers //
///////////////////////////

template<typename T>
class PerThreadContainer{
public:
    virtual void push(T x, int tid, uint64_t c) = 0;
    virtual void pop_all(void (*func)(T& x), uint64_t c) = 0;
    virtual bool try_pop_local(void (*func)(T& x), int tid, uint64_t c) = 0;
    virtual void pop_all_local(void (*func)(T& x), int tid, uint64_t c) = 0;
    virtual void clear() = 0;
    virtual ~PerThreadContainer(){}
};

template <typename T>
class CircBufferContainer: public PerThreadContainer<T>{
    padded<PerThreadCircBuffer<T>*> containers[4];
public:
    CircBufferContainer(int task_num){
        for (int i = 0; i < 4; i++){
            containers[i].ui = new PerThreadCircBuffer<T>(task_num);
        }
    }
    void push(T x, int tid, uint64_t c){
        containers[c%4].ui->push(x, tid);
    }
    void pop_all(void (*func)(T& x), uint64_t c){
        containers[c%4].ui->pop_all(func);
    }
    void pop_all_local(void (*func)(T& x), int tid, uint64_t c){
        assert(tid != -1);
        containers[c%4].ui->pop_all_local(func, tid);
    }
    bool try_pop_local(void (*func)(T& x), int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%4].ui->try_pop_local(func, tid);
    }
    void clear(){
        for (int i = 0; i < 4; i++){
            containers[i].ui->clear();
        }
    }
};

template <typename T>
class FixedCircBufferContainer: public PerThreadContainer<T>{
    padded<PerThreadFixedCircBuffer<T>*> containers[4];
public:
    FixedCircBufferContainer(int task_num, int cap){
        for (int i = 0; i < 4; i++){
            containers[i].ui = new PerThreadFixedCircBuffer<T>(task_num, cap);
        }
    }
    void push(T x, int tid, uint64_t c){
        containers[c%4].ui->push(x, tid);
    }
    bool try_push(T x, int tid, uint64_t c){
        return containers[c%4].ui->try_push(x, tid);
    }
    void pop_all(void (*func)(T& x), uint64_t c){
        containers[c%4].ui->pop_all(func);
    }
    void pop_all_local(void (*func)(T& x), int tid, uint64_t c){
        assert(tid != -1);
        containers[c%4].ui->pop_all_local(func, tid);
    }
    bool try_pop_local(void (*func)(T& x), int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%4].ui->try_pop_local(func, tid);
    }
    bool full_local(int tid, uint64_t c){
        return containers[c%4].ui->full_local(tid);
    }
    void clear(){
        for (int i = 0; i < 4; i++){
            containers[i].ui->clear();
        }
    }
};

template <typename T>
class VectorContainer: public PerThreadContainer<T>{
    padded<PerThreadVector<T>*> containers[4];
public:
    VectorContainer(int task_num){
        for (int i = 0; i < 4; i++){
            containers[i].ui = new PerThreadVector<T>(task_num);
        }
    }
    void push(T x, int tid, uint64_t c){
        containers[c%4].ui->push(x, tid);
    }
    void pop_all(void (*func)(T& x), uint64_t c){
        containers[c%4].ui->pop_all(func);
    }
    void pop_all_local(void (*func)(T& x), int tid, uint64_t c){
        assert(tid != -1);
        containers[c%4].ui->pop_all_local(func, tid);
    }
    bool try_pop_local(void (*func)(T& x), int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%4].ui->try_pop_local(func, tid);
    }
    void clear(){
        for (int i = 0; i < 4; i++){
            containers[i].ui->clear();
        }
    }
};

template <typename T, typename Hash = std::hash<T>>
class HashSetContainer: public PerThreadContainer<T>{
    padded<PerThreadHashSet<T, Hash>*> containers[4];
public:
    HashSetContainer(int task_num){
        for (int i = 0; i < 4; i++){
            containers[i].ui = new PerThreadHashSet<T, Hash>(task_num);
        }
    }
    void push(T x, int tid, uint64_t c){
        containers[c%4].ui->push(x, tid);
    }
    void pop_all(void (*func)(T& x), uint64_t c){
        containers[c%4].ui->pop_all(func);
    }
    void pop_all_local(void (*func)(T& x), int tid, uint64_t c){
        assert(tid != -1);
        containers[c%4].ui->pop_all_local(func, tid);
    }
    bool try_pop_local(void (*func)(T& x), int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%4].ui->try_pop_local(func, tid);
    }
    void clear(){
        for (int i = 0; i < 4; i++){
            containers[i].ui->clear();
        }
    }
};

}

#endif
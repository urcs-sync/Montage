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
    virtual void pop_all(const std::function<void(T& x)>& func, uint64_t c) = 0;
    virtual bool try_pop_local(const std::function<void(T& x)>& func, int tid, uint64_t c) = 0;
    virtual void pop_all_local(const std::function<void(T& x)>& func, int tid, uint64_t c) = 0;
    virtual void clear() = 0;
    virtual ~PerThreadContainer(){}
};

template <typename T>
class CircBufferContainer: public PerThreadContainer<T>{
    padded<PerThreadCircBuffer<T>*> containers[EPOCH_WINDOW];
public:
    CircBufferContainer(int task_num){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i].ui = new PerThreadCircBuffer<T>(task_num);
        }
    }
    void push(T x, int tid, uint64_t c){
        containers[c%EPOCH_WINDOW].ui->push(x, tid);
    }
    void pop_all(const std::function<void(T& x)>& func, uint64_t c){
        containers[c%EPOCH_WINDOW].ui->pop_all(func);
    }
    void pop_all_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        containers[c%EPOCH_WINDOW].ui->pop_all_local(func, tid);
    }
    bool try_pop_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%EPOCH_WINDOW].ui->try_pop_local(func, tid);
    }
    void clear(){
        for (int i = 0; i < 4; i++){
            containers[i].ui->clear();
        }
    }
};

template <typename T>
class FixedCircBufferContainer: public PerThreadContainer<T>{
    padded<PerThreadFixedCircBuffer<T>*> containers[EPOCH_WINDOW];
public:
    FixedCircBufferContainer(int task_num, size_t cap){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i].ui = new PerThreadFixedCircBuffer<T>(task_num, cap);
        }
    }
    void push(T x, int tid, uint64_t c){
        errexit("always use FixedCircBuffer::try_push().");
    }
    bool try_push(T x, int tid, uint64_t c){
        return containers[c%EPOCH_WINDOW].ui->try_push(x, tid);
    }
    void pop_all(const std::function<void(T& x)>& func, uint64_t c){
        containers[c%EPOCH_WINDOW].ui->pop_all(func);
    }
    void pop_all_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        containers[c%EPOCH_WINDOW].ui->pop_all_local(func, tid);
    }
    bool try_pop_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%EPOCH_WINDOW].ui->try_pop_local(func, tid);
    }
    void clear(){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i].ui->clear();
        }
    }
};

template <typename T>
class VectorContainer: public PerThreadContainer<T>{
    padded<PerThreadVector<T>*> containers[EPOCH_WINDOW];
public:
    VectorContainer(int task_num){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i].ui = new PerThreadVector<T>(task_num);
        }
    }
    void push(T x, int tid, uint64_t c){
        containers[c%EPOCH_WINDOW].ui->push(x, tid);
    }
    void pop_all(const std::function<void(T& x)>& func, uint64_t c){
        containers[c%EPOCH_WINDOW].ui->pop_all(func);
    }
    void pop_all_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        containers[c%EPOCH_WINDOW].ui->pop_all_local(func, tid);
    }
    bool try_pop_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%EPOCH_WINDOW].ui->try_pop_local(func, tid);
    }
    void clear(){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i].ui->clear();
        }
    }
};

template <typename T, typename Hash = std::hash<T>>
class HashSetContainer: public PerThreadContainer<T>{
    padded<PerThreadHashSet<T, Hash>*> containers[EPOCH_WINDOW];
public:
    HashSetContainer(int task_num){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i].ui = new PerThreadHashSet<T, Hash>(task_num);
        }
    }
    void push(T x, int tid, uint64_t c){
        containers[c%EPOCH_WINDOW].ui->push(x, tid);
    }
    void pop_all(const std::function<void(T& x)>& func, uint64_t c){
        containers[c%EPOCH_WINDOW].ui->pop_all(func);
    }
    void pop_all_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        containers[c%EPOCH_WINDOW].ui->pop_all_local(func, tid);
    }
    bool try_pop_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%EPOCH_WINDOW].ui->try_pop_local(func, tid);
    }
    void clear(){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i].ui->clear();
        }
    }
};

}

#endif
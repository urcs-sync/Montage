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
    virtual bool try_push(T x, int tid, uint64_t c) {errexit("try_push not implemented."); return false;};
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

template <typename T>
class FixedCircBufferContainer: public PerThreadContainer<T>{
    int thread_cnt;
    FixedCircBuffer<T>** containers[EPOCH_WINDOW];
public:
    FixedCircBufferContainer(int task_num, size_t cap){
        thread_cnt = task_num;
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i] = new FixedCircBuffer<T>*[thread_cnt];
            for (int j = 0; j < thread_cnt; j++){
                containers[i][j] = new FixedCircBuffer<T>(cap);
            }
        }
    }
    ~FixedCircBufferContainer(){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            for (int j = 0; j < thread_cnt; j++){
                delete containers[i][j];
            }
            delete containers[i];
        }
    }
    void push(T x, int tid, uint64_t c){
        errexit("always use FixedCircBuffer::try_push().");
    }
    bool try_push(T x, int tid, uint64_t c){
        return containers[c%EPOCH_WINDOW][tid]->try_push(x);
    }
    void pop_all(const std::function<void(T& x)>& func, uint64_t c){
        for (int i = 0; i < thread_cnt; i++){
            containers[c%EPOCH_WINDOW][i]->pop_all(func);
        }
    }
    void pop_all_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        containers[c%EPOCH_WINDOW][tid]->pop_all(func);
    }
    bool try_pop_local(const std::function<void(T& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%EPOCH_WINDOW][tid]->try_pop(func);
    }
    void clear(){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            for (int j = 0; j < thread_cnt; j++){
                containers[i][j]->clear();
            }
        }
    }
};

class FixedHashSetContainer: public PerThreadContainer<pds::pair<void*, size_t>>{
    int thread_cnt;
    FixedHashSet** containers[EPOCH_WINDOW];
public:
    FixedHashSetContainer(int task_num, size_t cap, const std::function<void(pds::pair<void*, size_t>& x)>& func){
        thread_cnt = task_num;
        for (int i = 0; i < EPOCH_WINDOW; i++){
            containers[i] = new FixedHashSet*[thread_cnt];
            for (int j = 0; j < thread_cnt; j++){
                containers[i][j] = new FixedHashSet(cap, func);
            }
        }
    }
    ~FixedHashSetContainer(){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            for (int j = 0; j < thread_cnt; j++){
                delete containers[i][j];
            }
            delete containers[i];
        }
    }
    void push(pds::pair<void*, size_t> x, int tid, uint64_t c){
        errexit("always use FixedHashSet::try_push().");
    }
    bool try_push(pds::pair<void*, size_t> x, int tid, uint64_t c){
        return containers[c%EPOCH_WINDOW][tid]->try_push(x);
    }
    void pop_all(const std::function<void(pds::pair<void*, size_t>& x)>& func, uint64_t c){
        for (int i = 0; i < thread_cnt; i++){
            containers[c%EPOCH_WINDOW][i]->pop_all(func);
        }
    }
    void pop_all_local(const std::function<void(pds::pair<void*, size_t>& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        containers[c%EPOCH_WINDOW][tid]->pop_all(func);
    }
    bool try_pop_local(const std::function<void(pds::pair<void*, size_t>& x)>& func, int tid, uint64_t c){
        assert(tid != -1);
        return containers[c%EPOCH_WINDOW][tid]->try_pop(func);
    }
    void clear(){
        for (int i = 0; i < EPOCH_WINDOW; i++){
            for (int j = 0; j < thread_cnt; j++){
                containers[i][j]->clear();
            }
        }
    }
};

}

#endif
#ifndef MOD_QUEUE_HPP
#define MOD_QUEUE_HPP

// use NVM in the map
#define IMMER_USE_NVM true

#include "TestConfig.hpp"
#include "RQueue.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include <mutex>
#include "immer/queue.hpp"
#include <array>

class MODQueue : public RQueue<std::string>{
    immer::queue<std::array<char,TESTS_VAL_SIZE>>* immer_queue;
    std::mutex lock;
public:
    MODQueue(GlobalTestConfig* gtc){
        char* heap_prefix = (char*) malloc(L_cuserid+11);
        strcpy(heap_prefix,"/mnt/pmem/");
        cuserid(heap_prefix+strlen("/mnt/pmem/"));
        nvm_initialize(heap_prefix, 0);
        free(heap_prefix);
        immer_queue = new immer::queue<std::array<char,TESTS_VAL_SIZE>>();
    };
    optional<std::string> dequeue(int tid){
        optional<std::string> ret = {};
        lock.lock();
        auto* old_queue = immer_queue;
        if(!immer_queue->isEmpty())
            immer_queue = immer_queue->pop_front_ptr();
        _mm_sfence();
        lock.unlock();
        if(!old_queue->isEmpty()) {
            //!write and old isn't empty, reclaiming front item and old queue
            const auto& front = old_queue->front();
            ret = std::string(std::begin(front), std::end(front));
            old_queue->delete_front();
            delete old_queue;
        }
        return ret;
    }

    void enqueue(std::string val, int tid){
        lock.lock();
        auto* old_queue = immer_queue;
        std::array<char,TESTS_VAL_SIZE> value;
        std::copy(val.begin(), val.end(), value.data());
        immer_queue = immer_queue->push_back_ptr(value);
        _mm_sfence();
        lock.unlock();
        delete old_queue;
    }
};

class MODQueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MODQueue(gtc);
    }
};

#endif
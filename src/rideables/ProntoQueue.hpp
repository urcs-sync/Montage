#ifndef PRONTO_QUEUE_HPP
#define PRONTO_QUEUE_HPP

#include "TestConfig.hpp"
#include "RQueue.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include <mutex>
#include <array>
#include "savitar.hpp"
#include <uuid/uuid.h>
#include <deque>

class ProntoQueue : public PersistentObject, public RQueue<std::string> {
public:
    typedef std::array<char,TESTS_VAL_SIZE> T;
    typedef std::deque<T, STLAlloc<T>> QueueType;

    ProntoQueue(uuid_t id) : PersistentObject(id) {
        void *t = alloc->alloc(sizeof(QueueType));
        QueueType *obj = (QueueType *)t;
        v_queue = new(obj) QueueType(STLAlloc<T>(alloc));
    }

    void enqueue(std::string val, int tid){
        // <compiler>
        Savitar_thread_notify(3, this, PushTag, val.data());
        // </compiler>
        T tmp;
        std::copy(val.begin(),val.end(),tmp.data());
        lock_guard<mutex> lk(lock);
        v_queue->push_back(tmp);
        // <compiler>
        Savitar_thread_wait(this, this->log);
        // </compiler>
    }

    /* pop the element at head, null if empty */
    optional<std::string> dequeue(int tid){
        // <compiler>
        Savitar_thread_notify(2, this, PopTag);
        // </compiler>
        optional<std::string> ret = {};
        lock_guard<mutex> lk(lock);
        if(!v_queue->empty()){
            const auto& res = v_queue->front();
            ret = std::string(std::begin(res),std::end(res));
            v_queue->pop_front();
        }
        // <compiler>
        Savitar_thread_wait(this, this->log);
        // </compiler>
        return ret;
    }

    // <compiler>
    ProntoQueue() : PersistentObject(true) {}

    static PersistentObject *BaseFactory(uuid_t id) {
        ObjectAlloc *alloc = GlobalAlloc::getInstance()->newAllocator(id);
        void *temp = alloc->alloc(sizeof(ProntoQueue));
        ProntoQueue *obj = (ProntoQueue *)temp;
        ProntoQueue *object = new (obj) ProntoQueue(id);
        return object;
    }

    static PersistentObject *RecoveryFactory(NVManager *m, CatalogEntry *e) {
        return BaseFactory(e->uuid);
    }

    static ProntoQueue *Factory(uuid_t id) {
        NVManager &manager = NVManager::getInstance();
        manager.lock();
        ProntoQueue *obj = (ProntoQueue *)manager.findRecovered(id);
        if (obj == NULL) {
            obj = static_cast<ProntoQueue *>(BaseFactory(id));
            manager.createNew(classID(), obj);
        }
        manager.unlock();
        return obj;
    }

    uint64_t Log(uint64_t tag, uint64_t *args) {
        int vector_size = 0;
        ArgVector vector[4]; // Max arguments of the class

        switch (tag) {
            case PushTag:
                {
                vector[0].addr = &tag;
                vector[0].len = sizeof(tag);
                vector[1].addr = (void *)args[0];
                vector[1].len = strlen((char *)args[0]) + 1;
                vector_size = 2;
                }
                break;
            case PopTag:
                {
                vector[0].addr = &tag;
                vector[0].len = sizeof(tag);
                vector_size = 1;
                }
                break;
            default:
                assert(false);
                break;
        }

        return AppendLog(vector, vector_size);
    }

    size_t Play(uint64_t tag, uint64_t *args, bool dry) {
        size_t bytes_processed = 0;
        switch (tag) {
            case PushTag:
                {
                char *value = (char *)args;
                if (!dry) enqueue(std::string(value),0);
                bytes_processed = strlen(value) + 1;
                }
                break;
            case PopTag:
                {
                if (!dry) dequeue(0);
                }
                break;
            default:
                {
                PRINT("Unknown tag: %zu\n", tag);
                assert(false);
                }
                break;
        }
        return bytes_processed;
    }

    static uint64_t classID() { return 1; }
    // </compiler>

private:
    QueueType* v_queue;
    mutex lock;
    // <compiler>
    enum MethodTags {
        PushTag = 1,
        PopTag = 2,
    };
    // </compiler>
};

class ProntoQueueFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        uuid_t uuid;
        uuid_generate(uuid);
        return ProntoQueue::Factory(uuid);
    }
};

#endif
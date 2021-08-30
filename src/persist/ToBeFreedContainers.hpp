#ifndef TO_BE_FREED_CONTAINERS_HPP
#define TO_BE_FREED_CONTAINERS_HPP

#include <cstdint>

#include "TestConfig.hpp"
#include "PerThreadContainers.hpp"

///////////////////////////
// To-be-free Containers //
///////////////////////////

namespace pds{

class PBlk;
class EpochSys;

class ToBeFreedContainer{
public:
    virtual void register_free(PBlk* blk, uint64_t c) {};
    virtual void help_free(uint64_t c) {};
    virtual void help_free_local(uint64_t c) {};
    virtual void clear() = 0;
    virtual void free_on_new_epoch(uint64_t c){};
    virtual ~ToBeFreedContainer(){}
};

class ThreadLocalFreedContainer : public ToBeFreedContainer{
    PerThreadContainer<PBlk*>* container = nullptr;
    padded<uint64_t>* threadEpoch;
    padded<std::mutex>* locks = nullptr;
    int task_num;
    EpochSys* _esys = nullptr;
    void do_free(PBlk*& x, uint64_t c);
public:
    ThreadLocalFreedContainer(EpochSys* e):_esys(e){}
    ThreadLocalFreedContainer(EpochSys* e, GlobalTestConfig* gtc);
    ~ThreadLocalFreedContainer();

    void free_on_new_epoch(uint64_t c);

    void register_free(PBlk* blk, uint64_t c);
    void help_free(uint64_t c);
    void help_free_local(uint64_t c);
    void clear();
};

class PerEpochFreedContainer : public ToBeFreedContainer{
    PerThreadContainer<PBlk*>* container = nullptr;
    EpochSys* _esys = nullptr;
    void do_free(PBlk*& x, uint64_t c);
   public:
    PerEpochFreedContainer(EpochSys* e):_esys(e){
        // errexit("DO NOT USE DEFAULT CONSTRUCTOR OF ToBeFreedContainer");
    }
    PerEpochFreedContainer(EpochSys* e, GlobalTestConfig* gtc);
    ~PerEpochFreedContainer();
    void free_on_new_epoch(uint64_t c){}
    void register_free(PBlk* blk, uint64_t c);
    void help_free(uint64_t c);
    void help_free_local(uint64_t c);
    void clear();
};

class NoToBeFreedContainer : public ToBeFreedContainer{
    // A to-be-freed container that does absolutely nothing.
    EpochSys* _esys = nullptr;
public:
    NoToBeFreedContainer(EpochSys* e):_esys(e){}
    virtual void register_free(PBlk* blk, uint64_t c);
    void free_on_new_epoch(uint64_t c){}
    virtual void help_free(uint64_t c){}
    virtual void help_free_local(uint64_t c){}
    virtual void clear(){}
};

}

#endif

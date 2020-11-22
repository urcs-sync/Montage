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

class ToBeFreedContainer{
public:
    virtual void register_free(PBlk* blk, uint64_t c) {};
    virtual void help_free(uint64_t c) {};
    virtual void help_free_local(uint64_t c) {};
    virtual void clear() = 0;
    virtual void free_on_new_epoch(uint64_t c){};
    virtual ~ToBeFreedContainer(){}
};

class PerThreadFreedContainer : public ToBeFreedContainer{
    PerThreadContainer<PBlk*>* container = nullptr;
    padded<uint64_t>* threadEpoch;
    padded<std::mutex>* locks = nullptr;
    int task_num;
    static void do_free(PBlk*& x);
public:
    PerThreadFreedContainer(){}
    PerThreadFreedContainer(GlobalTestConfig* gtc);
    ~PerThreadFreedContainer();

    void free_on_new_epoch(uint64_t c);

    void register_free(PBlk* blk, uint64_t c);
    void help_free(uint64_t c);
    void help_free_local(uint64_t c);
    void clear();
};

class PerEpochFreedContainer : public ToBeFreedContainer{
    PerThreadContainer<PBlk*>* container = nullptr;
    static void do_free(PBlk*& x);
public:
    PerEpochFreedContainer(){
        // errexit("DO NOT USE DEFAULT CONSTRUCTOR OF ToBeFreedContainer");
    }
    PerEpochFreedContainer(GlobalTestConfig* gtc);
    ~PerEpochFreedContainer();
    void free_on_new_epoch(uint64_t c){}
    void register_free(PBlk* blk, uint64_t c);
    void help_free(uint64_t c);
    void help_free_local(uint64_t c);
    void clear();
};

class NoToBeFreedContainer : public ToBeFreedContainer{
    // A to-be-freed container that does absolutely nothing.
public:
    NoToBeFreedContainer(){}
    virtual void register_free(PBlk* blk, uint64_t c);
    void free_on_new_epoch(uint64_t c){}
    virtual void help_free(uint64_t c){}
    virtual void help_free_local(uint64_t c){}
    virtual void clear(){}
};

}

#endif

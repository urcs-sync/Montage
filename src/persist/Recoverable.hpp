#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

#include "TestConfig.hpp"
// TODO: report recover errors/exceptions

class Recoverable{
public:
    Recoverable(){} // TODO: get rid of default constructor.
    Recoverable(GlobalTestConfig* gtc){
        // init Persistent allocator
        Persistent::init();
        // init epoch system
        pds::init(gtc);
        // init main thread
        pds::init_thread(0);
    }
    ~Recoverable(){
        pds::finalize();
        Persistent::finalize();
    }
    void init_thread(GlobalTestConfig*, LocalTestConfig* ltc){
        pds::init_thread(ltc->tid);
    }
    // return num of blocks recovered.
    virtual int recover(bool simulated = false) = 0;
};

#endif
#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "pblk_naked.hpp"
// TODO: report recover errors/exceptions

class Recoverable{
public:
    pds::EpochSys* _esys = nullptr;
    Recoverable(GlobalTestConfig* gtc);
    ~Recoverable();
    void init_thread(GlobalTestConfig*, LocalTestConfig* ltc);
    // return num of blocks recovered.
    virtual int recover(bool simulated = false) = 0;
};


#endif
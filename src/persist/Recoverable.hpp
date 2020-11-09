#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "pblk_naked.hpp"
// TODO: report recover errors/exceptions

class Recoverable{
    pds::EpochSys* _esys = nullptr;
public:
    Recoverable(GlobalTestConfig* gtc);
    ~Recoverable();
    void init_thread(GlobalTestConfig*, LocalTestConfig* ltc);
    void register_alloc_pblk(pds::PBlk* pblk);
    // return num of blocks recovered.
    virtual int recover(bool simulated = false) = 0;
};


#endif
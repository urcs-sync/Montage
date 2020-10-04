#ifndef PARALLEL_LAUNCH_HPP
#define PARALLEL_LAUNCH_HPP

#ifndef _REENTRANT
#define _REENTRANT
#endif


#include <pthread.h>
#include <sys/mman.h>
#include <stdlib.h> 
#include "HarnessUtils.hpp"
#include "TestConfig.hpp"
    

void parallelWork(GlobalTestConfig* gtc);

#endif

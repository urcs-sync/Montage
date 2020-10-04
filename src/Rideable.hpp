#ifndef RIDEABLE_HPP
#define RIDEABLE_HPP

#include <string>
#include "TestConfig.hpp"

#ifndef _REENTRANT
#define _REENTRANT		/* basic 3-lines for threads */
#endif


class GlobalTestConfig;

class Rideable{
public:
	virtual ~Rideable(){};
};


class Reportable{
public:
	virtual void introduce(){};
	virtual void conclude(){};
};

class RideableFactory{
public:
	virtual Rideable* build(GlobalTestConfig* gtc)=0;
	virtual ~RideableFactory(){};
};

#endif

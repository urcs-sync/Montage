#ifndef INTRUDERTEST_HPP
#define INTRUDERTEST_HPP

#include <pthread.h>
#include <iostream>
#include <cstring>
#include <cstdio>
#include "transaction.hpp"
#include "qstm.hpp"
#include "testconfig.hpp"
#include "intruder.h"

class IntruderTest : public TransTest{
	int thread_cnt;
public:
	static QSTMFactory* stm_factory;
	static TestConfig* tc;
	static int contention;//1 for high, 0 for low.
	// Spawns threads, joins threads, displays counter
	IntruderTest(TestConfig* c){
		tc = c;
		stm_factory = tc->getTMFactory();
		thread_cnt = tc->thread_cnt;
		contention = 0;
		if (tc->checkEnv("cont")){
			contention = std::stoi(tc->getEnv("cont"));
		}
	}
	void run(){
		char* argv[6];
		int argc = 6;

		argv[0]=(char*)malloc(11*sizeof(char));
		argv[1]=(char*)malloc(15*sizeof(char));
		argv[2]=(char*)malloc(15*sizeof(char));
		argv[3]=(char*)malloc(15*sizeof(char));
		argv[4]=(char*)malloc(15*sizeof(char));
		argv[5]=(char*)malloc(15*sizeof(char));

		strcpy(argv[0], "./intruder");
		sprintf(argv[1], "-t%d",thread_cnt);
		if(contention){//non-simulator
			strcpy(argv[2], "-a10");
			strcpy(argv[3], "-l128");
			strcpy(argv[4], "-n262144");
		} else {//simulator
			strcpy(argv[2], "-a10");
			strcpy(argv[3], "-l128");
			strcpy(argv[4], "-n100000");
		}
		strcpy(argv[5], "-s1");
		intruder_run(argc, argv, tc);
	}

	void finalize(){
		// pthread_exit(NULL);
	}
};

QSTMFactory* IntruderTest::stm_factory;
TestConfig* IntruderTest::tc;
int IntruderTest::contention = 0;

class IntruderTestFactory : public TransTestFactory{
	TransTest* build(TestConfig* tc){
		return new IntruderTest(tc);
	}
};



#endif

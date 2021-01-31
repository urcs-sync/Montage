#ifndef VACATIONTEST_HPP
#define VACATIONTEST_HPP

#include <pthread.h>
#include <iostream>
#include <cstring>
#include <cstdio>
//#include "../ringstm.hpp"
#include "transaction.hpp"
#include "qstm.hpp"
#include "testconfig.hpp"
#include "vacation.h"

class VacationTest : public TransTest{
	int thread_cnt;
public:
	static QSTMFactory* stm_factory;
	static TestConfig* tc;
	static int contention;//1 for high, 0 for low.
	// Spawns threads, joins threads, displays counter
	VacationTest(TestConfig* c){
		tc = c;
		stm_factory = tc->getTMFactory();
		thread_cnt = tc->thread_cnt;
		if (tc->checkEnv("cont")){
			contention = std::stoi(tc->getEnv("cont"));
		}
	}
	void run(){
		char* argv[7];
		int argc = 7;

		argv[0]=(char*)malloc(11*sizeof(char));
		argv[1]=(char*)malloc(15*sizeof(char));
		argv[2]=(char*)malloc(15*sizeof(char));
		argv[3]=(char*)malloc(15*sizeof(char));
		argv[4]=(char*)malloc(15*sizeof(char));
		argv[5]=(char*)malloc(15*sizeof(char));
		argv[6]=(char*)malloc(15*sizeof(char));

		strcpy(argv[0], "./vacation");
		sprintf(argv[1], "-t%d",thread_cnt);
		if(contention){//high contention
			strcpy(argv[2], "-n10");
			strcpy(argv[3], "-q60");
			strcpy(argv[4], "-u90");
		} else {
			strcpy(argv[2], "-n5");
			strcpy(argv[3], "-q90");
			strcpy(argv[4], "-u98");
		}
		strcpy(argv[5], "-r16384");
		strcpy(argv[6], "-x1000000");
		vacation_run(argc, argv, tc);
	}

	void finalize(){
		// pthread_exit(NULL);
	}
};

QSTMFactory* VacationTest::stm_factory;
TestConfig* VacationTest::tc;
int VacationTest::contention = 0;

class VacationTestFactory : public TransTestFactory{
	TransTest* build(TestConfig* tc){
		return new VacationTest(tc);
	}
};



#endif

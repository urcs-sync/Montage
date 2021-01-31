#include <pthread.h>
#include <iostream>
#include "transaction.hpp"
#include "countertest.hpp"
#include "ncounterstest.hpp"
#include "vacationtest.hpp"
#include "intrudertest.hpp"

#include "qstm.hpp"
#include "testlaunch.hpp"

#include <fcntl.h>
#include <sys/mman.h>

#define HEAPFILE "/dev/shm/gc_heap"

char *base_addr = NULL;
static char *curr_addr = NULL;

int main(int argc, char** argv){

	RP_init("test", 10737418240);

	TestConfig* tc = new TestConfig();

	tc->addTest(new CounterTestFactory(), "Single counter");
	tc->addTest(new NCountersTestFactory(), "N counters (-dN=<number>)");
	tc->addTest(new VacationTestFactory(), "Vacation (-dcont=<0|1>	0 for low, 1 for high)");
	tc->addTest(new IntruderTestFactory(), "Intruder (-dcont=<0|1>	0 for simulator, 1 for non-simulator)");

	tc->stm = new QSTMFactory();
	tc->stm_name="QSTM";

	std::cout << "STM: " << tc->stm_name << std::endl;

	tc->thread_cnt = 16;
	tc->parseCommandline(argc, argv);

	testLaunch(tc);
	delete(tc);

	RP_close();
}

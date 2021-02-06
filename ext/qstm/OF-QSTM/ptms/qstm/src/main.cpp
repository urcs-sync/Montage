#include <pthread.h>
#include <iostream>
//#include "../ringstm.hpp"
#include "transaction.hpp"
#include "countertest.hpp"
#include "ncounterstest.hpp"
#include "vacationtest.hpp"
#include "intrudertest.hpp"

#include "ringstm.hpp"
#include "qstm-old.hpp"
#include "qstm.hpp"
#include "testlaunch.hpp"

// #include "Romulus.hpp"
// mspace Romulus_p_ms {};
// const uint8_t* base_addr = (uint8_t*)0x7fdd00000000;
// const uint64_t max_size = 8*1024*1024*1024ul; // 8 Gb for the user
// void p_init(){
//	 const char* MMAP_FILENAME = "/localdisk/segment/romulus_shared";
//	 // File doesn't exist
//	 int fd = open(MMAP_FILENAME, O_RDWR|O_CREAT, 0755);
//	 assert(fd >= 0);
//	 if (lseek(fd, max_size-1, SEEK_SET) == -1) {
//		 perror("lseek() error");
//	 }
//	 if (write(fd, "", 1) == -1) {
//		 perror("write() error");
//	 }
//	 // mmap() memory range
//	 uint8_t* got_addr = (uint8_t *)mmap(base_addr, max_size, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, 0);
//	 if (got_addr == MAP_FAILED) {
//		 printf("got_addr = %p  %p\n", got_addr, MAP_FAILED);
//		 perror("ERROR: mmap() is not working !!! ");
//		 assert(false);
//	 }
//	 // No data in persistent memory, initialize
//	 romulus::g_main_size = max_size;
//	 romulus::g_main_addr = base_addr;

//	 Romulus_p_ms = romulus::create_mspace_with_base(romulus::g_main_addr, romulus::g_main_size, true);
// }
// Spawns threads, joins threads, displays counter


//#include "makalu.h"
#include <fcntl.h>
#include <sys/mman.h>

#define HEAPFILE "/dev/shm/gc_heap_hbeadle"

//char *base_addr = NULL;
static char *curr_addr = NULL;

/*void __map_persistent_region(){
	int fd;
	fd  = open(HEAPFILE, O_RDWR | O_CREAT | O_TRUNC,
				  S_IRUSR | S_IWUSR);

	off_t offt = lseek(fd, MAKALU_FILESIZE-1, SEEK_SET);
	assert(offt != -1);

	int result = write(fd, "", 1);
	assert(result != -1);

	void * addr =
		mmap(0, MAKALU_FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	assert(addr != MAP_FAILED);

	*((intptr_t*)addr) = (intptr_t) addr;
	base_addr = (char*) addr;
	//adress to remap to, the root pointer to gc metadata, 
	//and the curr pointer at the end of the day
	curr_addr = (char*) ((size_t)addr + 3 * sizeof(intptr_t));
	printf("Addr: %p\n", addr);
	printf("Base_addr: %p\n", base_addr);
	printf("Current_addr: %p\n", curr_addr);
}

int __nvm_region_allocator(void** memptr, size_t alignment, size_t size)
{
	char* next;
	char* res;
	if (size < 0) return 1;

	if (((alignment & (~alignment + 1)) != alignment)  ||	//should be multiple of 2
		(alignment < sizeof(void*))) return 1; //should be atleast the size of void*
	size_t aln_adj = (size_t) curr_addr & (alignment - 1);

	if (aln_adj != 0)
		curr_addr += (alignment - aln_adj);

	res = curr_addr;
	next = curr_addr + size;
	if (next > base_addr + MAKALU_FILESIZE){
		printf("\n----Ran out of space in mmaped file-----\n");
		return 1;
	}
	curr_addr = next;
	*memptr = res;
	//printf("Current NVM Region Addr: %p\n", curr_addr);

	return 0;
}

*/
int main(int argc, char** argv){
	// p_init();
//	__map_persistent_region();
//	MAK_start(&__nvm_region_allocator);

	std::cout << "Min region size: " << MIN_SB_REGION_SIZE << std::endl;
	std::cout << "Max region size: " << MAX_SB_REGION_SIZE << std::endl;
	//RP_init("test", MIN_SB_REGION_SIZE+1024); // TODO set these parameters?
	RP_init("test", 10737418240);

	TestConfig* tc = new TestConfig();

	tc->addTest(new CounterTestFactory(), "Single counter");
	tc->addTest(new NCountersTestFactory(), "N counters (-dN=<number>)");
	tc->addTest(new VacationTestFactory(), "Vacation (-dcont=<0|1>	0 for low, 1 for high)");
	tc->addTest(new IntruderTestFactory(), "Intruder (-dcont=<0|1>	0 for simulator, 1 for non-simulator)");

	// tc->addSTM(new RingSTMFactory(), "RingSTM");
	// tc->addSTM(new QSTMFactory(), "QSTM");
#if defined(RINGSTM)
	tc->stm = new RingSTMFactory();
	tc->stm_name="RingSTM";
#elif defined(OLDQUEUESTM)
	tc->stm = new OLDQSTMFactory();
	tc->stm_name="OLDQSTM";
#elif defined(QUEUESTM)
	tc->stm = new QSTMFactory();
	tc->stm_name="QSTM";
#endif

	std::cout << "STM: " << tc->stm_name << std::endl;

	tc->thread_cnt = 16;
	tc->parseCommandline(argc, argv);

	testLaunch(tc);
	delete(tc);

	RP_close();
}

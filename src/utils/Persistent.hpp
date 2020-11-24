#ifndef PERSISTENT_HPP
#define PERSISTENT_HPP

#include <cstdio>
#include <cstring>
#include <AllocatorMacro.hpp>
#include <ralloc.hpp>
#include <HarnessUtils.hpp>

using namespace std;

class Persistent {
public:
	void* operator new(size_t size){
		// cout<<"persistent allocator called."<<endl;
		// void* ret = malloc(size);
		void* ret = RP_malloc(size);
		if (!ret){
			cerr << "Persistent::new failed: no free memory" << endl;
			exit(1);
		}
		return ret;
	}

	void* operator new (std::size_t size, void* ptr) {
		return ptr;
	}

	void operator delete(void * p) { 
		RP_free(p); 
	} 

	static void init(){
		// pm_init();
		char* heap_prefix = (char*) malloc(L_cuserid+6);
		cuserid(heap_prefix);
		strcat(heap_prefix, "_test");
		RP_init(heap_prefix, REGION_SIZE);
		free(heap_prefix);
		// init main thread
		Ralloc::set_tid(0);
		// TODO: deal with returned value.
	}
	static void finalize(){
		// pm_close();
		RP_close();
	}
	static size_t get_malloc_size(void* ptr){
		return RP_malloc_size(ptr);
	}
	// n: number of iterators it's going to return
	static std::vector<InuseRecovery::iterator> recover(int n){
		char* heap_prefix = (char*) malloc(L_cuserid+6);
		cuserid(heap_prefix);
		strcat(heap_prefix, "_test");
		if (RP_init(heap_prefix, REGION_SIZE) != 1){
			errexit("not a restart of ralloc.");
		}
		free(heap_prefix);
		return RP_recover(n);
	}
	static InuseRecovery::iterator recover(){
		return recover(1).at(0);
	}
	static void simulate_crash(int tid){
		RP_simulate_crash(tid);
	}

	static void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
		Ralloc::set_tid(ltc->tid);
	}
};

#endif

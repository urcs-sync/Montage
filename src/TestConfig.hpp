#ifndef GLOBALTESTCONFIG_HPP
#define GLOBALTESTCONFIG_HPP

#include <string.h>
#include <vector>
#include <map>
#include <unistd.h>
#include <iostream>
#include <sys/time.h>
#include <math.h>
#include <assert.h>
#include <malloc.h>
// #include <sys/time.h>
#include <chrono>
#include <sys/resource.h>
#include <hwloc.h>

#include "HarnessUtils.hpp"
#include "Rideable.hpp"
#include "Recorder.hpp"

#ifndef TESTS_KEY_SIZE
  #define TESTS_KEY_SIZE 32
#endif

#ifndef TESTS_VAL_SIZE
  #define TESTS_VAL_SIZE 1024
#endif

class Test;
class Rideable;
class RideableFactory;

class GlobalTestConfig{
public:
	int task_num = 4;  // number of threads
	std::chrono::time_point<std::chrono::high_resolution_clock> start, finish; // timing structures
	double interval = 2.0;  // number of seconds to run test
	uint64_t parInit_time = 0; // number of seconds to run parInit in total 

	std::vector<hwloc_obj_t> affinities; // map from tid to CPU id
	hwloc_topology_t topology;
	
	int num_procs=24;
	Test* test=NULL;
	int testType=0;
	int rideableType=0;
	int verbose=0;
	unsigned long warmup = 3; // MBs of data to warm
	bool timeOut = true; // whether to abort on infinte loop
	std::string affinity;
	
	Recorder* recorder = NULL;
	std::vector<RideableFactory*> rideableFactories;
	std::vector<std::string> rideableNames;
	std::vector<Test*> tests;
	std::vector<std::string> testNames;
	std::string outFile;
	std::vector<Rideable*> allocatedRideables;

	long int total_operations=0;


	
	GlobalTestConfig();
	~GlobalTestConfig();

	// allocate test object at runtime.
	Test* allocTest();

	// for tests to access rideable objects
	Rideable* allocRideable();


	// for configuration set up
	void parseCommandLine(int argc, char** argv);
	std::string getRideableName();
	void addRideableOption(RideableFactory* r, const char name[]);
	std::string getTestName();
	void addTestOption(Test* t, const char name[]);

	// for accessing environment and args
	void setEnv(std::string,std::string);
	bool checkEnv(std::string);
	std::string getEnv(std::string);

	void setArg(std::string,void*);
	bool checkArg(std::string);
	void* getArg(std::string);
	
	// for affinity modifications
	// TODO: make a general-purpose unit of this.
	void buildPerCoreAffinity(std::vector<hwloc_obj_t>& aff, unsigned pu);
	void buildInterleavedPerCoreAffinity(std::vector<hwloc_obj_t>& aff, unsigned pu);
	void buildSingleSocketPerCoreAffinity(std::vector<hwloc_obj_t>& aff, unsigned pu);

	// Run the test
	void runTest();

	std::map<std::string,std::string> environment;
	void printargdef();
	// TODO: add affinity builders.
	// // Affinity functions
	// // a bunch needed because of recursive traversal of topologies.
	void buildAffinity(std::vector<hwloc_obj_t>& aff);
private:
	void extendAffinity(std::vector<hwloc_obj_t>& aff);
	void buildDFSAffinity_helper(std::vector<hwloc_obj_t>& aff, hwloc_obj_t obj);
	void buildDFSAffinity(std::vector<hwloc_obj_t>& aff);
	int buildDefaultAffinity_findCoresInSocket(std::vector<hwloc_obj_t>& aff, hwloc_obj_t obj, std::vector<hwloc_obj_t>* cores);
	int buildDefaultAffinity_buildPUsInCores(std::vector<hwloc_obj_t>& aff, std::vector<hwloc_obj_t>* cores);
	int buildDefaultAffinity_findAndBuildSockets(std::vector<hwloc_obj_t>& aff, hwloc_obj_t obj);
	void buildDefaultAffinity(std::vector<hwloc_obj_t>& aff);
	void buildSingleAffinity_helper(std::vector<hwloc_obj_t>& aff, hwloc_obj_t obj);
	void buildSingleAffinity(std::vector<hwloc_obj_t>& aff);
	void buildSingleSocketAffinity(std::vector<hwloc_obj_t>& aff);
	void buildPerCoreAffinity_helper(std::vector<hwloc_obj_t>& aff, unsigned pu, hwloc_obj_t obj);
	void buildInterleavedAffinity(std::vector<hwloc_obj_t>& aff);
	void buildInterleavedAffinity_traversePackages(std::vector<std::vector<hwloc_obj_t>>& thread_per_package, hwloc_obj_t obj);
	void buildInterleavedPerCoreAffinity_traversePackages(std::vector<std::vector<hwloc_obj_t>>& thread_per_package, unsigned pu, hwloc_obj_t obj);

	std::map<std::string,void*> arguments;
	
	void createTest();
	char* argv0;

};

// local test configuration, one per thread
class LocalTestConfig{
public:
	int tid;
	unsigned int seed;
	unsigned cpu;
	hwloc_cpuset_t cpuset;
};

class CombinedTestConfig{
public:
	GlobalTestConfig* gtc;
	LocalTestConfig* ltc;
};

class Test{
public:
	// called by one (master) thread
	virtual void init(GlobalTestConfig* gtc)=0;
	virtual void cleanup(GlobalTestConfig* gtc)=0;

	// called by all threads in parallel
	virtual void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){}
	// runs the test
	// returns number of operations completed by that thread
	virtual int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc)=0;
	virtual ~Test(){}
};


#endif

#ifndef PINNING_HPP
#define PINNING_HPP

#include <iostream>
#include <unistd.h>
#include <vector>
#include <map>
#include <hwloc.h>
#include <vector>

//#include "transaction.hpp"
//#include "concurrentprimitives.hpp"

void errexit(std::string s);

	// Affinity functions
	// a bunch needed because of recursive traversal of topologies.
class PinConfig{
public:
void buildAffinity(int thd_num);
void buildDFSAffinity_helper(hwloc_obj_t obj);
void buildDFSAffinity();
int buildDefaultAffinity_findCoresInSocket(hwloc_obj_t obj, std::vector<hwloc_obj_t>* cores);
int buildDefaultAffinity_buildPUsInCores(std::vector<hwloc_obj_t>* cores);
int buildDefaultAffinity_findAndBuildSockets(hwloc_obj_t obj);
void buildDefaultAffinity();
void buildSingleAffinity_helper(hwloc_obj_t obj);
void buildSingleAffinity();
void buildSingleSocketAffinity();

std::vector<hwloc_obj_t> affinities; // map from tid to CPU id
hwloc_topology_t topology;
int num_procs=24;
std::string affinity;

std::vector<std::string> test_names;
std::map<std::string,std::string> environment;

unsigned int test = 0;
int thread_cnt = 1;
bool verbose = 0;

void setEnv(std::string key, std::string value);
bool checkEnv(std::string key);
std::string getEnv(std::string key);

void parseCommandline(int argc, char** argv);
};

#endif

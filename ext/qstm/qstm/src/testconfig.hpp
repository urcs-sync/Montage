#ifndef TESTCONFIG_HPP
#define TESTCONFIG_HPP

#include <iostream>
#include <unistd.h>
#include <vector>
#include <map>
#include <hwloc.h>
#include <vector>

#include "transaction.hpp"
#include "concurrentprimitives.hpp"

class RingSTMFactory;
class QSTMFactory;
class OLDQSTMFactory;
class TransTestFactory;

void errexit(std::string s);

class TestConfig{
private:
	// Affinity functions
	// a bunch needed because of recursive traversal of topologies.
	void buildAffinity();
	void buildDFSAffinity_helper(hwloc_obj_t obj);
	void buildDFSAffinity();
	int buildDefaultAffinity_findCoresInSocket(hwloc_obj_t obj, std::vector<hwloc_obj_t>* cores);
	int buildDefaultAffinity_buildPUsInCores(std::vector<hwloc_obj_t>* cores);
	int buildDefaultAffinity_findAndBuildSockets(hwloc_obj_t obj);
	void buildDefaultAffinity();
	void buildSingleAffinity_helper(hwloc_obj_t obj);
	void buildSingleAffinity();

public:
	std::vector<hwloc_obj_t> affinities; // map from tid to CPU id
	hwloc_topology_t topology;
	int num_procs=24;
	std::string affinity;

	std::vector<TransTestFactory*> tests;
	std::vector<std::string> test_names;
	// std::vector<TMFactory*> stms;
	// std::vector<std::string> stm_names;
#if defined(RINGSTM)
	RingSTMFactory* stm;
#elif defined(OLDQUEUESTM)
	OLDQSTMFactory* stm;
#elif defined(QUEUESTM)
	QSTMFactory* stm;
#endif
	std::string stm_name;
	std::map<std::string,std::string> environment;

	unsigned int test = 0;
	// unsigned int stm = 0;
	int thread_cnt = 1;
	bool verbose = 0;

	void addTest(TransTestFactory* t, std::string name){
		tests.push_back(t);
		test_names.push_back(name);

	}
	// void addSTM(TMFactory* s, std::string name){
	// 	stms.push_back(s);
	// 	stm_names.push_back(name);
	// }

	void help(){
		std::cerr<<"usage: [-v][-t thread_cnt][-T test][-d env=val][-h]"<<std::endl;
		for (unsigned int i = 0; i < tests.size(); i++){
			std::cerr<<"Test "<<i<<" : "<<test_names[i]<<std::endl;
		}
		// for (unsigned int i = 0; i < stms.size(); i++){
		// 	std::cerr<<"STM "<<i<<" : "<<stm_names[i]<<std::endl;
		// }
	}

	void setEnv(std::string key, std::string value);
	bool checkEnv(std::string key);
	std::string getEnv(std::string key);

	void parseCommandline(int argc, char** argv);

#if defined(RINGSTM)
	RingSTMFactory* getTMFactory() {
		return stm;
	}
#elif defined(OLDQUEUESTM)
	OLDQSTMFactory* getTMFactory() {
		return stm;
	}
#elif defined(QUEUESTM)
	QSTMFactory* getTMFactory() {
		return stm;
	}
#endif

	~TestConfig();
};



#endif

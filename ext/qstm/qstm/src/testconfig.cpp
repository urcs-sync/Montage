#include "testconfig.hpp"
#include <vector>


void errexit(std::string s){
	std::cerr<<s<<std::endl;
	exit(1);
}

void TestConfig::parseCommandline(int argc, char** argv){
	if(argc==1){
		help();
		errexit("");
	}

	if(tests.size()==0){
		errexit("No test options provided. Use testConfig::addTest() to add.");
	}
	// Read command line
	char c;
	while ((c = getopt (argc, argv, "t:T:s:d:hv")) != -1){
		switch (c) {
			case 'v':
			 	this->verbose = 1;
			 	break;
			case 't':
				this->thread_cnt = atoi(optarg);
				if (thread_cnt <= 0){
					errexit("thread_cnt should be >= 1.\n");
				}
				break;
			case 'T':
				this->test = atoi(optarg);
				if(test>=tests.size()){
					fprintf(stderr, "Invalid test (-T) option.\n");
					help();
					errexit("");
				}
				break;
			case 'd':{
				std::string s = std::string(optarg);
				std::string k,v;
				std::string::size_type pos = s.find('=');
				if (pos != std::string::npos){
					k = s.substr(0, pos);
					v = s.substr(pos+1, std::string::npos);
				}
				else{
				  k = s; v = "1";
				}
				if(v=="true"){v="1";}
				if(v=="false"){v="0";}
				environment[k]=v;}
				break;
			case 'h':
				help();
				exit(1);
			default:
				help();
				exit(1);
     	}
	}

	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);
	num_procs = hwloc_get_nbobjs_by_depth(topology,  
	 hwloc_get_type_depth(topology, HWLOC_OBJ_PU));
	buildAffinity();

}

void TestConfig::setEnv(std::string key, std::string value){
	if(verbose){
		std::cout <<"setEnv: "<< key << " = \"" << value << "\"\n";
	}
	environment[key]=value;
}
bool TestConfig::checkEnv(std::string key){
	if(verbose){
		std::cout <<"checkEnv: "<< key << "\n";
	}
	return environment[key]!="" && environment[key]!="0";
}
std::string TestConfig::getEnv(std::string key){
	if(verbose){
		std::cout <<"getEnv: "<< key << "\n";
	}
	return environment[key];
}

TestConfig::~TestConfig(){
	for(auto iter = tests.begin(), end = tests.end(); iter != end; ++iter){
		delete *iter;
	}
}


// based on
// https://www.open-mpi.org/projects/hwloc/doc/v1.7/
void TestConfig::buildDFSAffinity_helper(hwloc_obj_t obj){
	if(obj->type==HWLOC_OBJ_PU){
		affinities.push_back(obj);
		return;
	}
	if(affinities.size()>=thread_cnt){return;}
	for (unsigned i = 0; i < obj->arity; i++) {
			buildDFSAffinity_helper(obj->children[i]);
	}
}

void TestConfig::buildDFSAffinity(){
	buildDFSAffinity_helper(hwloc_get_root_obj(topology));
}

int TestConfig::buildDefaultAffinity_findCoresInSocket(hwloc_obj_t obj, std::vector<hwloc_obj_t>* cores){
	if(obj->type==HWLOC_OBJ_CORE){
		cores->push_back(obj);
		return 1;
	}
	if(obj->type==HWLOC_OBJ_PU){
		return 0; // error: we shouldn't reach PU's before cores...
	}
	int ret = 1;
	for (unsigned i = 0; i < obj->arity; i++) {
			ret = ret && buildDefaultAffinity_findCoresInSocket(obj->children[i],cores);
	}
	return ret;
}

int TestConfig::buildDefaultAffinity_buildPUsInCores(std::vector<hwloc_obj_t>* cores){
		int coreIndex = 0;
		int coresFilled = 0;
		while(coresFilled<cores->size()){
			for(int i = 0; i<cores->size(); i++){
				// so stride over cores, expecting that next level down are PUs
				if(coreIndex==cores->at(i)->arity){
						coresFilled++;
						continue;
				}
				hwloc_obj_t obj = cores->at(i)->children[coreIndex];
				if(obj->type!=HWLOC_OBJ_PU){return 0;}
				affinities.push_back(obj);
			}
			coreIndex++;
		}
		return 1;
}

// descend to socket level, then build each socket individually
int TestConfig::buildDefaultAffinity_findAndBuildSockets(hwloc_obj_t obj){
	// recursion terminates at sockets
	if(obj->type==HWLOC_OBJ_SOCKET){
		std::vector<hwloc_obj_t> cores;
		if(!buildDefaultAffinity_findCoresInSocket(obj,&cores)){
			return 0; // couldn't find cores in this socket, so flag error
		}
		// now "cores" is filled with all cores below the socket,
		// so assign threads to this core
		return buildDefaultAffinity_buildPUsInCores(&cores);
	}
	// recursion down by DFS
	int ret = 1;
	for (unsigned i = 0; i < obj->arity; i++) {
			ret = ret && buildDefaultAffinity_findAndBuildSockets(obj->children[i]);
	}
	return ret;
}

void TestConfig::buildDefaultAffinity(){
	if(!buildDefaultAffinity_findAndBuildSockets(hwloc_get_root_obj(topology))){
		fprintf(stderr, "Unsupported topology for default thread pinning (unable to detect sockets and cores).");
		fprintf(stderr, "Switching to depth first search affinity.\n");
		affinities.resize(0);
		buildDFSAffinity();	
	}
}

void TestConfig::buildSingleAffinity_helper(hwloc_obj_t obj){
	if(obj->type==HWLOC_OBJ_PU){
		for(int i =0; i<thread_cnt; i++){
			affinities.push_back(obj);
		}
		return;
	}
	buildSingleAffinity_helper(obj->children[0]);
}

void TestConfig::buildSingleAffinity(){
	buildSingleAffinity_helper(hwloc_get_root_obj(topology));
}

// reference:
// https://www.open-mpi.org/projects/hwloc//doc/v1.2.2/hwloc_8h.php
void TestConfig::buildAffinity(){
	if(affinity.compare("dfs")==0){
		buildDFSAffinity();
	}
	else if(affinity.compare("single")==0){
		buildSingleAffinity();
	}
	else{
		buildDefaultAffinity();
	}
	if(affinities.size()<thread_cnt){
		affinities.resize(thread_cnt);
	}
	for(int i=num_procs; i<thread_cnt; i++){
		affinities[i] = affinities[i%num_procs];
	}
}

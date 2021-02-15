#include <iostream>
#include <string>
#include <thread>
#include <random>
#include <time.h>
#include <sys/time.h>

#include "ConcurrentPrimitives.hpp"
#include "HarnessUtils.hpp"
// #include "RSTMRBTreeMap.hpp"

// #include "RSTMRBTree.hpp"
// #include "RSTMLinkList.hpp"
// #include "RSTMHMap.hpp"

#if !defined(MNEMOSYNE) and !defined(PRONTO)
// #include "MCASRBTree.hpp"
// #include "MCASLinkList.hpp"
// #include "MCASHMap.hpp"
#include "MontageMSQueue.hpp"
#include "MontageQueue.hpp"
#include "MODQueue.hpp"
#include "Queue.hpp"
#include "MSQueue.hpp"
#include "PriorityQueue.hpp"

// #include "LinkedList.hpp"
// #include "HOHHashTable.hpp"
#include "HashTable.hpp"
#include "MontageHashTable.hpp"
#include "UnbalancedTree.hpp"
#include "SOFTHashTable.hpp"
#include "NVMSOFTHashTable.hpp"
#include "MODHashTable.hpp"

#include "DaliUnorderedMap.hpp"
#include "FriedmanQueue.hpp"

#include "MontageLfHashTable.hpp"
#include "MontageNatarajanTree.hpp"

#include "LockfreeHashTable.hpp"
#include "PLockfreeHashTable.hpp"
#include "NatarajanTree.hpp"

#include "TGraph.hpp"
#include "NVMGraph.hpp"
// #include "DLGraph.hpp"
#include "MontageGraph.hpp"
#endif

#ifdef MNEMOSYNE
#include "MneQueue.hpp"
#include "MneHashTable.hpp"
#endif

#ifdef PRONTO
#include "ProntoQueue.hpp"
#include "ProntoHashTable.hpp"
#endif
// #include "Toy.hpp"
#include "StackVerify.hpp"
#include "StackTest.hpp"
#include "QueueTest.hpp"
#include "KVTest.hpp"
#include "YCSBTest.hpp"
//#include "GraphTest.hpp"

#include "QueueChurnTest.hpp"
#include "HeapChurnTest.hpp"
#include "SetChurnTest.hpp"
#include "MapTest.hpp"
#include "MapChurnTest.hpp"
#include "SyncTest.hpp"
#ifndef MNEMOSYNE
#include "RecoverVerifyTest.hpp"
// #include "GraphRecoveryTest.hpp"
#include "TGraphConstructionTest.hpp"
#include "ToyTest.hpp"
#endif /* !MNEMOSYNE */
#include "AllocTest.hpp"
#include "CustomTypes.hpp"

#include "TreiberStack.hpp"
#include "PTreiberStack.hpp"
#include "MontageStack.hpp"

using namespace std;


int main(int argc, char *argv[])
{
	GlobalTestConfig gtc;
	const int numVertices = 1000000;
	const int meanEdgesPerVertex = 32;
	const int vertexLoad = 50;


	/* stacks */
	gtc.addRideableOption(new TreiberStackFactory<string>(), "TreiberStack");
	gtc.addRideableOption(new PTreiberStackFactory<string>(), "PTreiberStack");
	gtc.addRideableOption(new MontageStackFactory<string>(), "MontageTreiberStack");



	/* stacks */
	gtc.addRideableOption(new TreiberStackFactory<string>(), "TreiberStack");
	gtc.addRideableOption(new PTreiberStackFactory<string>(), "PTreiberStack");
	gtc.addRideableOption(new MontageStackFactory<string>(), "MontageTreiberStack");


	/* queues */
	// gtc.addRideableOption(new MSQueueFactory<string>(), "MSQueue");//transient
	// gtc.addRideableOption(new FriedmanQueueFactory<string>(), "FriedmanQueue");//comparison
	// gtc.addRideableOption(new MontageMSQueueFactory<string>(), "MontageMSQueue");
#if !defined(MNEMOSYNE) and !defined(PRONTO)
	gtc.addRideableOption(new QueueFactory<string,PLACE_DRAM>(), "TransientQueue<DRAM>");
	gtc.addRideableOption(new QueueFactory<string,PLACE_NVM>(), "TransientQueue<NVM>");
	gtc.addRideableOption(new MontageQueueFactory<string>(), "MontageQueue");
	gtc.addRideableOption(new MODQueueFactory(), "MODQueue");

	/* mappings */
	gtc.addRideableOption(new LockfreeHashTableFactory<string>(), "LfHashTable");//transient
	gtc.addRideableOption(new PLockfreeHashTableFactory(), "PLockfreeHashTable");
	gtc.addRideableOption(new NatarajanTreeFactory<string>(), "NataTree");//transient
	gtc.addRideableOption(new DaliUnorderedMapFactory<string>(), "Dali");//comparison
	gtc.addRideableOption(new HashTableFactory<string,PLACE_DRAM>(), "TransientHashTable<DRAM>");
	gtc.addRideableOption(new HashTableFactory<string,PLACE_NVM>(), "TransientHashTable<NVM>");
	gtc.addRideableOption(new MontageHashTableFactory<string>(), "MontageHashTable");
	gtc.addRideableOption(new SOFTHashTableFactory<string>(), "SOFT");
	gtc.addRideableOption(new MODHashTableFactory<string>(), "MODHashTable");
	gtc.addRideableOption(new NVMSOFTHashTableFactory<string>(), "NVMSOFT");
	gtc.addRideableOption(new MontageLfHashTableFactory<string>(), "MontageLfHashTable");
	gtc.addRideableOption(new MontageNatarajanTreeFactory<string>(), "MontageNataTree");

	/* graphs */
	gtc.addRideableOption(new TGraphFactory<numVertices, meanEdgesPerVertex, vertexLoad>(), "TGraph");
	gtc.addRideableOption(new NVMGraphFactory<numVertices, meanEdgesPerVertex, vertexLoad>(), "NVMGraph");
	// gtc.addRideableOption(new DLGraphFactory<numVertices>(), "DLGraph");
	gtc.addRideableOption(new MontageGraphFactory<numVertices, meanEdgesPerVertex, vertexLoad>(), "MontageGraph");

    // gtc.addRideableOption(new MontageGraphFactory<3072627>(), "Orkut");
    gtc.addRideableOption(new TGraphFactory<3076727, 0, 100>(), "TransientOrkut");
#endif /* !defined(MNEMOSYNE) and !defined(PRONTO) */
#ifdef MNEMOSYNE
	gtc.addRideableOption(new MneQueueFactory<string>(), "MneQueue");
	gtc.addRideableOption(new MneHashTableFactory<string>(), "MneHashTable");
#endif
#ifdef PRONTO
	gtc.addRideableOption(new ProntoQueueFactory(), "ProntoQueue");
	gtc.addRideableOption(new ProntoHashTableFactory(), "ProntoHashTable");
#endif
	gtc.addTestOption(new StackVerify(90,10), "StackVerify");
	gtc.addTestOption(new StackTest(50,50,2000), "Stack:push50pop50:prefill=2000");
	gtc.addTestOption(new QueueChurnTest(50,50,2000), "QueueChurn:eq50dq50:prefill=2000");
	gtc.addTestOption(new QueueTest(5000000,50), "Queue:5m");
	gtc.addTestOption(new MapChurnTest<string,string>(0, 0, 50, 50, 1000000, 500000), "MapChurnTest<string>:g0p0i50rm50:range=1000000:prefill=500000");
	gtc.addTestOption(new MapChurnTest<string,string>(50, 0, 25, 25, 1000000, 500000), "MapChurnTest<string>:g50p0i25rm25:range=1000000:prefill=500000");
	gtc.addTestOption(new MapChurnTest<string,string>(90, 0, 5, 5, 1000000, 500000), "MapChurnTest<string>:g90p0i5rm5:range=1000000:prefill=500000");
	gtc.addTestOption(new MapTest<string,string>(0, 0, 50, 50, 1000000, 500000, 10000000), "MapTest<string>:g0p0i50rm50:range=1000000:prefill=500000:op=10000000");
	gtc.addTestOption(new MapTest<string,string>(50, 0, 25, 25, 1000000, 500000, 10000000), "MapTest<string>:g50p0i25rm25:range=1000000:prefill=500000:op=10000000");
	gtc.addTestOption(new MapTest<string,string>(90, 0, 5, 5, 1000000, 500000, 10000000), "MapTest<string>:g90p0i5rm5:range=1000000:prefill=500000:op=10000000");
	gtc.addTestOption(new MapSyncTest<string, string>(0, 0, 50, 50, 1000000, 500000), "MapSyncTest<string>:g0p0i50rm50:range=1000000:prefill=500000");
#ifndef MNEMOSYNE
	gtc.addTestOption(new RecoverVerifyTest<string,string>(), "RecoverVerifyTest");

//	gtc.addTestOption(new GraphTest(numVertices, meanEdgesPerVertex,vertexLoad,8000), "GraphTest:80edge20vertex:degree32");
//	gtc.addTestOption(new GraphTest(numVertices, meanEdgesPerVertex,vertexLoad,9980), "GraphTest:99.8edge.2vertex:degree32");
	// gtc.addTestOption(new GraphRecoveryTest("graph_data/", "orkut-edge-list_", 28610, 5, true), "GraphRecoveryTest:Orkut:verify");
    // gtc.addTestOption(new GraphRecoveryTest("graph_data/", "orkut-edge-list_", 28610, 5, false), "GraphRecoveryTest:Orkut:noverify");
    gtc.addTestOption(new TGraphConstructionTest("graph_data/", "orkut-edge-list_", 28610, 5), "TGraphConstructionTest:Orkut");
#endif /* !MNEMOSYNE */
	gtc.addTestOption(new AllocTest(1024 * 1024, DO_JEMALLOC_ALLOC), "AllocTest-JEMalloc");
	gtc.addTestOption(new AllocTest(1024 * 1024, DO_RALLOC_ALLOC), "AllocTest-Ralloc");
	gtc.addTestOption(new AllocTest(1024 * 1024, DO_MONTAGE_ALLOC), "AllocTest-Montage");

	gtc.parseCommandLine(argc, argv);
	
        omp_set_num_threads(gtc.task_num);
	gtc.runTest();

	// print out results
	if(gtc.verbose){
		printf("Operations/sec: %ld\n",(uint64_t)(gtc.total_operations/gtc.interval));
	}
	else{
		string rideable_name = gtc.getRideableName().c_str();
		if(gtc.getEnv("PersistStrat") == "No") {
			rideable_name = "NoPersist"+rideable_name;
		}
#ifdef PRONTO
  #ifdef PRONTO_SYNC
		rideable_name = "Sync"+rideable_name;
  #else
		rideable_name = "Full"+rideable_name;
  #endif
#endif
		printf("%d,%ld,%s,%s\n",gtc.task_num,(uint64_t)(gtc.total_operations/gtc.interval),rideable_name.c_str(),gtc.getTestName().c_str());
	}
}

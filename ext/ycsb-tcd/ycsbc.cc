//
//  ycsbc.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/19/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include <cstring>
#include <string>
#include <iostream>
#include <vector>
#include <future>
#include "core/utils.h"
#include "core/timer.h"
#include "core/client.h"
#include "core/core_workload.h"
#include "db/db_factory.h"
#include "cache_test/cache_test.h"
#include "AllocatorMacro.hpp"

using namespace std;

#ifdef MONTAGE
#include "TestConfig.hpp"
#include "persist_struct_api.hpp"
#endif /* MONTAGE */

void UsageMessage(const char *command);
bool StrStartWith(const char *str, const char *pre);
string ParseCommandLine(int argc, char *argv[], utils::Properties &props);

int DelegateClient(ycsbc::DB *db, ycsbc::CoreWorkload *wl, const int num_ops,
    bool is_loading, unsigned tid) {
#ifdef THREAD_PINNING
  int task_id;
  int core_id;
  cpu_set_t cpuset;
  int set_result;
  int get_result;
  CPU_ZERO(&cpuset);
  task_id = tid;
  core_id = PINNING_MAP[task_id%80];
  CPU_SET(core_id, &cpuset);
  set_result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (set_result != 0){
    fprintf(stderr, "setaffinity failed for thread %d to cpu %d\n", task_id, core_id);
exit(1);
  }
  get_result = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (set_result != 0){
    fprintf(stderr, "getaffinity failed for thread %d to cpu %d\n", task_id, core_id);
exit(1);
  }
  if (!CPU_ISSET(core_id, &cpuset)){
  fprintf(stderr, "WARNING: thread aiming for cpu %d is pinned elsewhere.\n", core_id);	 
  } else {
    // fprintf(stderr, "thread pinning on cpu %d succeeded.\n", core_id);
  }
#endif
#ifdef MAKALU
  MAK_init_thread();
#endif

#ifdef MONTAGE
  pds::init_thread(tid);
#endif

  db->Init(tid);
  ycsbc::Client client(*db, *wl);
  int oks = 0;
  for (int i = 0; i < num_ops; ++i) {
    if (is_loading) {
      oks += client.DoInsert(tid);
    } else {
      oks += client.DoTransaction(tid);
    }
  }
  db->Close();
  return oks;
}

#ifdef TCD
#include "../threadcached/include/pku_memcached.h"
#endif
#include <math.h>

bool do_cache_test_flag = false;
unsigned cache_test_item_size = 0; 
int main(const int argc, char *argv[]) {
  // TODO: include GlobalTestConfig, construct command line args, and initialize EpochSys
  
  memcached_init();
  utils::Properties props;
  string file_name = ParseCommandLine(argc, argv, props);
#ifdef MONTAGE
  // Ralloc init and close are already handled by memcached_init() 
  // init epoch system with artificial gtc, only for passing t and d
  GlobalTestConfig gtc;
  int c;
  while ((c = getopt (argc, argv, "d:t:h:P")) != -1){
    switch (c) {
      case 't':
        gtc.task_num = atoi(optarg);
        break;
      case 'h':
        gtc.printargdef();
        break;
      case 'P':
        break;
      case 'd':
        string s = std::string(optarg);
        string k,v;
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
        gtc.environment[k]=v;
        break;
      }
  }
  hwloc_topology_init(&gtc.topology);
  hwloc_topology_load(gtc.topology);
  gtc.num_procs = hwloc_get_nbobjs_by_depth(gtc.topology,  
  hwloc_get_type_depth(gtc.topology, HWLOC_OBJ_PU));
  std::cout<<"initial affinity built"<<std::endl;
  gtc.buildAffinity(gtc.affinities);
  pds::init(&gtc);
#endif
  if (do_cache_test_flag){
    do_cache_test();
    memcached_close();
  }

  ycsbc::DB *db = ycsbc::DBFactory::CreateDB(props);
  if (!db) {
    cout << "Unknown database name " << props["dbname"] << endl;
    exit(0);
  }
  ycsbc::CoreWorkload wl;
  wl.Init(props);

  const int num_threads = stoi(props.GetProperty("threadcount", "1"));

  // Loads data
  vector<future<int>> actual_ops;
  int total_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
  // Wt: disable parallelism on loading data here
  // for (int i = 0; i < num_threads; ++i) {
  actual_ops.emplace_back(async(launch::async,
      DelegateClient, db, &wl, total_ops, true, 0));
  // }
  assert((int)actual_ops.size() == 1);

  int sum = 0;
  for (auto &n : actual_ops) {
    assert(n.valid());
    sum += n.get();
  }
  cout << "# Loading records:\t" << sum << endl;

  // Peforms transactions
  actual_ops.clear();
  total_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);
  utils::Timer<double> timer;
  timer.Start();
  for (int i = 0; i < num_threads; ++i) {
    actual_ops.emplace_back(async(launch::async,
        DelegateClient, db, &wl, total_ops / num_threads, false, i));
  }
  assert((int)actual_ops.size() == num_threads);
  sum = 0;
  for (auto &n : actual_ops) {
    assert(n.valid());
    sum += n.get();
  }
  double duration = timer.End();
  cout << "# Transaction throughput (KTPS)" << endl;
  cout << props["dbname"] << '\t' << file_name << '\t' << num_threads << '\t';
  cout << total_ops / duration / 1000 << endl;
  cerr << num_threads << "," << total_ops / duration / 1000;
  fflush(stdout);
  memcached_close();
}

string ParseCommandLine(int argc, char *argv[], utils::Properties &props) {
  int argindex = 1;
  string filename;
  while (argindex < argc && StrStartWith(argv[argindex], "-")) {
    if (strcmp(argv[argindex], "-t") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("threadcount", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-db") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("dbname", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-host") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("host", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-port") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("port", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-slaves") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("slaves", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-P") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      filename.assign(argv[argindex]);
      ifstream input(argv[argindex]);
      try {
        props.Load(input);
      } catch (const string &message) {
        cout << message << endl;
        exit(0);
      }
      input.close();
      argindex++;
    } else if (strcmp(argv[argindex], "-sz") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      cache_test_item_size = atoi(argv[argindex]);
      argindex++; 
    } else if (strcmp(argv[argindex], "-cache") == 0) {
      argindex++; 
      do_cache_test_flag = true;
    } else {
      cout << "Unknown option '" << argv[argindex] << "'" << endl;
      argindex++;
      // exit(0);
    }
  }

  if (argindex == 1 || argindex != argc) {
    UsageMessage(argv[0]);
    exit(0);
  }

  return filename;
}

void UsageMessage(const char *command) {
  cout << "Usage: " << command << " [options]" << endl;
  cout << "Options:" << endl;
  cout << "  -t n: execute using n threads (default: 1)" << endl;
  cout << "  -db dbname: specify the name of the DB to use (default: basic)" << endl;
  cout << "  -P propertyfile: load properties from the given file. Multiple files can" << endl;
  cout << "                   be specified, and will be processed in the order specified" << endl;
}

inline bool StrStartWith(const char *str, const char *pre) {
  return strncmp(str, pre, strlen(pre)) == 0;
}


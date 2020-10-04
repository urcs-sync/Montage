//
//  basic_db.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/17/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifdef MCD
#include "tcd_db.h"
#include <utility>
#include <assert.h>
#include <iostream>
#include <string.h>
#include <pthread.h>

// WARNING: we only consider 1 field. Workloads should be cooked such that they
// only give us 1 field!!!
memcached_st *structs[64];
bool initialized[64] = {0};
pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
memcached_st *parent = NULL;
namespace ycsbc {
  void TCDDB::Init(int tid) {
    if (initialized[tid]){
      memcached_free(structs[tid]);
    }
    pthread_mutex_lock(&init_mutex);
    if (parent == NULL){
      parent= memcached_create(NULL);
//      memcached_behavior_set(parent, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
      memcached_return_t rc= memcached_server_add(parent, "localhost", 11211);
      assert(rc == MEMCACHED_SUCCESS);

    }
    pthread_mutex_unlock(&init_mutex);
    structs[tid] = memcached_clone(NULL, parent);
  }
  int TCDDB::Read(const std::string &table, const std::string &key,
      const std::vector<std::string> *fields,
      std::vector<KVPair> &result, int tid) {
    size_t v_len;
    uint32_t flags;
    memcached_return_t err;
    char *cstr =  memcached_get(
        structs[tid],
        key.c_str(), key.size(),
        &v_len, &flags, &err); 
    if (err == MEMCACHED_SUCCESS && cstr != NULL) {
      result.push_back(std::make_pair(std::string("field0"), std::string(cstr, v_len))); 
    } else {
      printf("error: %s\n", memcached_strerror(structs[tid], err));
    }
    return DB::kOK;
  }

  int TCDDB::Scan(const std::string &table, const std::string &key,
      int len, const std::vector<std::string> *fields,
      std::vector<std::vector<KVPair>> &result, int tid) {
    assert(0 && "This is not implemented");
    return 0;
  }

  int TCDDB::Update(const std::string &table, const std::string &key,
      std::vector<KVPair> &values, int tid) {
    std::string value = values[0].second; 
    size_t v_len;
    uint32_t flags;
    memcached_return_t err = memcached_set(
        structs[tid],
        key.c_str(), key.size(),
        value.c_str(), value.size(), 
        0, 0);
    printf("success [%d]\n", err);
    if (err != MEMCACHED_SUCCESS && err != MEMCACHED_STORED){
      printf("error: %s\n", memcached_strerror(structs[tid], err));
    }
    return DB::kOK;

  }

  int TCDDB::Insert(const std::string &table, const std::string &key,
      std::vector<KVPair> &values, int tid) {
    return Update(table, key, values, tid); 
  }

  int TCDDB::Delete(const std::string &table, const std::string &key, int tid){
    assert(0 && "This is not implemented");
  }

};

#endif

//
//  basic_db.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/17/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//


// use these definitions only if we give the right flag
#ifdef TCD

#include "tcd_db.h"
#include <utility>
#include <assert.h>
#include <iostream>
#include <string.h>

//#define DEBUG 1

// WARNING: we only consider 1 field. Workloads should be cooked such that they
// only give us 1 field!!!
namespace ycsbc {
  void TCDDB::Init(int tid) {}

  int TCDDB::Read(const std::string &table, const std::string &key,
      const std::vector<std::string> *fields,
      std::vector<KVPair> &result, int tid) {
    size_t v_len;
    uint32_t flags;
    memcached_return_t err;
    auto kcstr = key.c_str();
    char *cstr =  memcached_get_internal(
        kcstr, strlen(kcstr),
        &v_len, &flags, &err); 
    if (err == MEMCACHED_SUCCESS) {
      result.push_back(std::make_pair(std::string("field0"), std::string(cstr, v_len))); 
      if (cstr != NULL) {
        free(cstr);
      }
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
    auto value = values[0].second; 
    size_t v_len;
    uint32_t flags;
    auto kcstr = key.c_str();
    auto vcstr = value.c_str();
    memcached_return_t err = memcached_set_internal(
        kcstr, strlen(kcstr),
        vcstr, strlen(kcstr), 
        0, 0);
    if (err != MEMCACHED_STORED)
      fprintf(stderr, "We didn't insert correctly...[%d]\n", err);
#if DEBUG
    else printf("success insert:\n\t%s\n\t%s\n", key.c_str(), value.c_str());
#endif
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

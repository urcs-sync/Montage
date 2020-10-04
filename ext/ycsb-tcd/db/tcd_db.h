//
//  basic_db.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/17/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_TCD_DB_H_
#define YCSB_C_TCD_DB_H_

#include "core/db.h"
#include <string>
#include "core/properties.h"

#ifdef TCD
#include <pku_memcached.h>
#else
#include <libmemcached/common.h>
#endif

// KVPair is std::pair<std::string, std::string>


namespace ycsbc {

class TCDDB : public DB {
  public:
  void Init(int tid);

  int Read(const std::string &table, const std::string &key,
      const std::vector<std::string> *fields,
      std::vector<KVPair> &result, int tid);

  int Scan(const std::string &table, const std::string &key,
      int len, const std::vector<std::string> *fields,
      std::vector<std::vector<KVPair>> &result, int tid);

  int Update(const std::string &table, const std::string &key,
      std::vector<KVPair> &values, int tid);

  int Insert(const std::string &table, const std::string &key,
      std::vector<KVPair> &values, int tid);

  int Delete(const std::string &table, const std::string &key, int tid);
};

} // ycsbc

#endif // YCSB_C_TCD_DB_H_


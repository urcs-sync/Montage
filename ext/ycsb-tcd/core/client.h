//
//  client.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/10/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_CLIENT_H_
#define YCSB_C_CLIENT_H_

#include <string>
#include "db.h"
#include "core_workload.h"
#include "utils.h"

namespace ycsbc {

class Client {
 public:
  Client(DB &db, CoreWorkload &wl) : db_(db), workload_(wl) { }
  
  virtual bool DoInsert(int tid);
  virtual bool DoTransaction(int tid);
  
  virtual ~Client() { }
  
 protected:
  
  virtual int TransactionRead(int tid);
  virtual int TransactionReadModifyWrite(int tid);
  virtual int TransactionScan(int tid);
  virtual int TransactionUpdate(int tid);
  virtual int TransactionInsert(int tid);
  
  DB &db_;
  CoreWorkload &workload_;
};

inline bool Client::DoInsert(int tid) {
  std::string key = workload_.NextSequenceKey();
  std::vector<DB::KVPair> pairs;
  workload_.BuildValues(pairs);
  return (db_.Insert(workload_.NextTable(), key, pairs, tid) == DB::kOK);
}

inline bool Client::DoTransaction(int tid) {
  int status = -1;
  switch (workload_.NextOperation()) {
    case READ:
      status = TransactionRead(tid);
      break;
    case UPDATE:
      status = TransactionUpdate(tid);
      break;
    case INSERT:
      status = TransactionInsert(tid);
      break;
    case SCAN:
      status = TransactionScan(tid);
      break;
    case READMODIFYWRITE:
      status = TransactionReadModifyWrite(tid);
      break;
    default:
      throw utils::Exception("Operation request is not recognized!");
  }
  assert(status >= 0);
  return (status == DB::kOK);
}

inline int Client::TransactionRead(int tid) {
  const std::string &table = workload_.NextTable();
  const std::string &key = workload_.NextTransactionKey();
  std::vector<DB::KVPair> result;
  if (!workload_.read_all_fields()) {
    std::vector<std::string> fields;
    fields.push_back("field" + workload_.NextFieldName());
    return db_.Read(table, key, &fields, result, tid);
  } else {
    return db_.Read(table, key, NULL, result, tid);
  }
}

inline int Client::TransactionReadModifyWrite(int tid) {
  const std::string &table = workload_.NextTable();
  const std::string &key = workload_.NextTransactionKey();
  std::vector<DB::KVPair> result;

  if (!workload_.read_all_fields()) {
    std::vector<std::string> fields;
    fields.push_back("field" + workload_.NextFieldName());
    db_.Read(table, key, &fields, result, tid);
  } else {
    db_.Read(table, key, NULL, result, tid);
  }

  std::vector<DB::KVPair> values;
  if (workload_.write_all_fields()) {
    workload_.BuildValues(values);
  } else {
    workload_.BuildUpdate(values);
  }
  return db_.Update(table, key, values, tid);
}

inline int Client::TransactionScan(int tid) {
  const std::string &table = workload_.NextTable();
  const std::string &key = workload_.NextTransactionKey();
  int len = workload_.NextScanLength();
  std::vector<std::vector<DB::KVPair>> result;
  if (!workload_.read_all_fields()) {
    std::vector<std::string> fields;
    fields.push_back("field" + workload_.NextFieldName());
    return db_.Scan(table, key, len, &fields, result, tid);
  } else {
    return db_.Scan(table, key, len, NULL, result, tid);
  }
}

inline int Client::TransactionUpdate(int tid) {
  const std::string &table = workload_.NextTable();
  const std::string &key = workload_.NextTransactionKey();
  std::vector<DB::KVPair> values;
  if (workload_.write_all_fields()) {
    workload_.BuildValues(values);
  } else {
    workload_.BuildUpdate(values);
  }
  return db_.Update(table, key, values, tid);
}

inline int Client::TransactionInsert(int tid) {
  const std::string &table = workload_.NextTable();
  const std::string &key = workload_.NextSequenceKey();
  std::vector<DB::KVPair> values;
  workload_.BuildValues(values);
  return db_.Insert(table, key, values, tid);
} 

} // ycsbc

#endif // YCSB_C_CLIENT_H_

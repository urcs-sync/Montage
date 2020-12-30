/*

Copyright 2020 University of Rochester

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
License for the specific language governing permissions and limitations under
the License.

*/

#ifndef CLEVEL_HPP
#define CLEVEL_HPP

#include "RCUTracker.hpp"
#include "RMap.hpp"
#include "Recoverable.hpp"
#include <atomic>
#include <vector>

const size_t slots_per_bucket = 8;

// These constants are stolen from LockfreeHashTable.hpp
const int epoch_freq = 100;
const int empty_freq = 1000;
const bool collect = true;

template <typename K, typename V> class CLevelHashTable : public RMap<K, V> {
  class Payload : public pds::PBlk {
    GENERATE_FIELD(K, key, Payload);
    GENERATE_FIELD(V, val, Payload);

  public:
    Payload() {}
    Payload(K x, V y) : m_key(x), m_val(y) {}
    Payload(const Payload &oth)
        : pds::PBlk(oth), m_key(oth.m_key), m_val(oth.m_val) {}
    void persist() {}
  } __attribute__((aligned(CACHELINE_SIZE)));

  // A reference to a payload, which allows
  // for the first 16 bits to be tag data
  struct PayloadRef {
  private:
    uint64_t data;

  public:
    PayloadRef(Payload *plp, size_t hash) {
      uint16_t tag = static_cast<uint16_t>(hash & 0xFFFF);
      this->data =
          static_cast<uint64_t>((static_cast<uint64_t>(tag) << 48) | plp);
    }

    // Returns the first 16 bits of the payload
    inline uint16_t tag() {
      // Takes the highest 16 bits of the data
      return static_cast<uint16_t>(this->data >> 48);
    }

    // Returns the pointer to the payload
    inline Payload *ptr() {
      // Masks off the lower 48 bits
      return static_cast<Payload *>(this->data & 0xffffffffffff);
    }

    // Checks if this reference is empty
    inline bool nullp() { return this->data == 0; }
  };

  struct Bucket {
    array<PayloadRef, slots_per_bucket> slots;
  };

  struct Level {
    vector<Bucket> buckets;
    Level *next;

    Level(size_t size) : buckets{size}, next{nullptr} {}
  };

  struct Context {
    atomic<Level *> first_level, last_level;
    bool is_resizing;
  };

  Context global_context;
  atomic<Context *> global_ctx_ptr;

  RCUTracker<Payload> tracker;

public:
  CLevelHashTable(int task_number)
      : tracker{task_number, epoch_freq, empty_freq, collect} {}

  optional<V> get(K key, int tid) {
    std::hash<K> hasher{};
    uint64_t hash = hasher(key);

    uint32_t top = static_cast<uint32_t>(hash >> 32);
    uint32_t bottom = static_cast<uint32_t>(hash & 0xffffffff);
    return optional<V>{};
  }

  optional<V> put(K key, V val, int tid) {
    errexit("Put not implemented");
    return optional<V>{};
  }

  bool insert(K key, V val, int tid) {
    errexit("Insert not implemented");
    return false;
  }

  optional<V> remove(K key, int tid) {
    errexit("Remove not implemented");
    return optional<V>{};
  }

  optional<V> replace(K key, V val, int tid) {
    errexit("Replace not implemented");
    return optional<V>{};
  }

  int recover(bool simulated) {
    errexit("Recover not implemented");
    return 0;
  }
};

template <class T> class CLevelHashTableFactory : public RideableFactory {
  Rideable *build(GlobalTestConfig *gtc) {
    return new CLevelHashTable<T, T>(gtc->task_num);
  }
};

#endif

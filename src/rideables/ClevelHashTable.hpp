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

#include "RMap.hpp"
#include "Recoverable.hpp"
#include <atomic>
#include <vector>

const size_t SLOTS_PER_BUCKET = 8;

template <typename K, typename V>
class CLevelHashTable : public RMap<K, V>, public Recoverable {
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
    inline uint16_t tag() {
      // Takes the highest 16 bits
      return static_cast<uint16_t>(this->data >> 48);
    }

    // Gets the pointer to the payload
    inline Payload *ptr() {
      // Masks off the lower 48 bits
      return static_cast<Payload *>(this->data & 0xffffffffffff);
    }

    // Checks if this reference is empty
    inline bool nullp() { return this->data == 0; }
  };

  struct Bucket {
    array<PayloadRef, SLOTS_PER_BUCKET> slots;
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
};

#endif

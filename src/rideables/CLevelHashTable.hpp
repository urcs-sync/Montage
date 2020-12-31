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

template <typename K, typename V, typename Hasher = std::hash<K>>
class CLevelHashTable : public RMap<K, V>, Recoverable {
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
      uint16_t tag = take_tag(hash);
      this->data =
          static_cast<uint64_t>((static_cast<uint64_t>(tag) << 48) | plp);
    }

    inline static uint16_t take_tag(size_t hash) {
      return static_cast<uint16_t>(hash & 0xFFFF);
    }

    // Returns the first 16 bits of the payload
    inline uint16_t tag() {
      // Takes the highest 16 bits of the data
      return static_cast<uint16_t>(this->data >> 48);
    }

    // Returns the pointer to the payload
    inline Payload *ptr() {
      // Masks off the lower 48 bits
      return reinterpret_cast<Payload *>(this->data & 0xffffffffffff);
    }

    // Checks if this reference is empty
    inline bool nullp() { return this->data == 0; }

    optional<V> match(K k, size_t hash) {
      if (nullp() || take_tag(hash) != tag())
        return optional<V>{};
      K key = ptr()->get_key();
      if (key == k)
        return ptr()->get_val();
    }
  };

  struct Bucket {
    array<PayloadRef, slots_per_bucket> slots;

    optional<V> search(K k, size_t hash) {
      for (PayloadRef ref : slots) {
        optional<V> res = ref.match(k, hash);
        if (res.has_value())
          return res;
      }
      return optional<V>{};
    }
  };

  struct Level {
    vector<Bucket> buckets;
    Level *next;

    Level(size_t size) : buckets{size}, next{nullptr} {}

    // Finds the object in this level with hash =hash=
    optional<V> search(K k, size_t hash) {
      // Half of the size of size_t, in bits
      constexpr size_t half_hash_size = (sizeof(size_t) * 8) / 2;

      // A mask that covers the lower 'half_hash_size' of a size_t
      constexpr size_t lower_half_mask =
          ((size_t)1 << half_hash_size) - (size_t)1;

      // The upper and lower halves of the hash
      size_t hash_lower_half = hash & lower_half_mask;
      size_t hash_upper_half = hash >> half_hash_size;

      size_t first_index = hash_upper_half % buckets.size();
      size_t second_index = hash_lower_half % buckets.size();

      optional<V> first_lookup = buckets[first_index].search(k, hash);

      if (first_lookup.has_value())
        return first_lookup;

      return buckets[second_index].search(k, hash);
    }
  };

  struct Context {
    Level *first_level, *last_level;
    bool is_resizing;
  };

  Context global_context;
  atomic<Context *> global_ctx_ptr;

  RCUTracker<Payload> tracker;

public:
  CLevelHashTable(int task_number)
      : tracker{task_number, epoch_freq, empty_freq, collect} {}

  optional<V> get(K key, int tid) {
    Hasher hasher{};
    size_t hash = hasher(key);
    optional<V> res;

    Context *ctx;

    do {
      ctx = global_ctx_ptr.load();

      for (Level *l = ctx->last_level; l != ctx->first_level; l = l->next) {
        res = l->search(key, hash);

        if (res.has_value())
          goto end;
      }

    } while (ctx == global_ctx_ptr.load());

  end:
    tracker.end_op(tid);
    return res;
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

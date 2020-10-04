#pragma once

#include <mutex> // For std::unique_lock
#include <shared_mutex>

#include <immer/map.hpp>
#include <immer/nvm_utils.hpp>

namespace immer {

// This concurrent wrapper itself is not persistent,
// it merely takes a persistent map in ctor.

template <typename K, typename T>
class concurrent_map 
{
  public: 
    using size_type = detail::hamts::size_t;

    concurrent_map (immer::map<K, T> **map) : map_ (map) {}

    void set(K k, T v) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto *old_map = *map_;
        *map_ = (*map_)->set_ptr(k, v);
        // We only need to persist the map pointer, map itself is persisted
        // in set_ptr.
        NVM_PERSIST(map_, 1);
        _mm_sfence();
        // Refcounting on or off?
        delete old_map;
    }

    const T* find(const K& k) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return (*map_)->find(k);
    }

    size_type size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return (*map_)->size();
    }
    void erase(K k) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto *old_map = *map_;
        *map_ = (*map_)->erase_ptr(k);
        // We only need to persist the map pointer, map itself is persisted
        // in set_ptr.
        NVM_PERSIST(map_, 1);
        _mm_sfence();
        // Refcounting on or off?
        delete old_map;
    }

  private:
      mutable std::shared_mutex mutex_;
      immer::map<K, T> **map_;
};

} // namespace immer

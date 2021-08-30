#ifndef CLEVEL_HASH_HPP
#define CLEVEL_HASH_HPP

#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>
#include "RMap.hpp"
#include "AllocatorMacro.hpp"
#include "clevel/clevel_hash.hpp"
#include "clevel/polymorphic_string.h"
#include <filesystem>
#include <unistd.h>
#include <iostream>

#define LAYOUT "clevel_hash"

namespace fs = std::filesystem;

// template<typename K, typename V>
class CLevelHashAdapter : public RMap<std::string, std::string> {
  using K = std::string;
  using V = std::string;
  class key_equal {
  public:
    template <typename M, typename U>
    bool operator()(const M &lhs, const U &rhs) const
    {
      return lhs == rhs;
    }
  };

  class string_hasher {
    /* hash multiplier used by fibonacci hashing */
    static const size_t hash_multiplier = 11400714819323198485ULL;

  public:
    using transparent_key_equal = key_equal;

    size_t operator()(const pmdk_montage::polymorphic_string &str) const
    {
      return hash(str.c_str(), str.size());
    }


  private:
    size_t hash(const char *str, size_t size) const
    {
      size_t h = 0;
      for (size_t i = 0; i < size; ++i) {
        h = static_cast<size_t>(str[i]) ^ (h * hash_multiplier);
      }
      return h;
    }
  };

  using string_t = pmdk_montage::polymorphic_string;
  using clevel_map = pmem::obj::experimental::clevel_hash<string_t, string_t, string_hasher,std::equal_to<string_t>, 12>;
  // init starts
  struct Root {
    pmem::obj::persistent_ptr<clevel_map> cons;
  };
  pmem::obj::pool<Root> pool;
  pmem::obj::persistent_ptr<clevel_map> hash;

  // fs::path temp_path() {
  //   return fs::path("/mnt/pmem") / fs::path(getlogin());
  // }
  
public:
  CLevelHashAdapter(GlobalTestConfig *gtc) {
    // if (fs::exists(temp_path()))
    //   fs::remove(temp_path());

    // Allocate the initial pool
    char* heap_prefix = (char*) malloc(L_cuserid+11);
    strcpy(heap_prefix,"/mnt/pmem/");
    cuserid(heap_prefix+strlen("/mnt/pmem/"));

    // Allocate the initial pool
    pool = pmem::obj::pool<Root>::create(heap_prefix, LAYOUT, REGION_SIZE, /* TODO: allocate more space if needed*/ S_IWUSR | S_IRUSR);
    free(heap_prefix);
    //pool = pmem::obj::pool<Root>::create(temp_path(), LAYOUT, REGION_SIZE, /* TODO: allocate more space if needed*/ S_IWUSR | S_IRUSR);

    // Pop off the root object from the pool
    auto proot = pool.root();
    // Start a transaction to init the pool information
    {
      pmem::obj::transaction::manual tx(pool);
      proot->cons = pmem::obj::make_persistent<clevel_map>();
      proot->cons->set_thread_num(gtc->task_num);
      pmem::obj::transaction::commit();
    }
    hash = proot->cons;
  }

  // Gets value corresponding to a key
  // returns : the most recent value set for that key
  optional<V> get(K key, int tid) {
    auto ret = hash->search(key);
    // if(ret.found){
    //   return ret.val;
    // }
    return optional<V>{};
  }
  
  // Puts a new key/value pair into the map   
  // returns : the previous value for this key,
  // or NULL if no such value exists
  optional<V> put(K key, V val, int tid) {
    std::pair<K, V> par{key, val};
    auto ret = hash->update(par, tid);
    
    return optional<V>{};
  }
  
  bool insert(K key, V val, int tid) {
    //cout << "here in insert" << endl;
    std::pair<K, V> par{key, val};
    auto ret = hash->insert(par, tid, 0); // The last item doesn't make sense?
    if(ret.found){
      return false;
    }
    return true;
  }
 
  // Removes a value corresponding to a key
  // returns : the removed value
  optional<V> remove(K key, int tid) {
    auto ret = hash->erase(key, tid);
    // if(ret.found){
    //   return ret.val;
    // }
    return optional<V>{};
  }
  
  // Replaces the value corresponding to a key
  // if the key is already present in the map
  // returns : the replaced value, or NULL if replace was unsuccessful
  optional<V> replace(K key, V val, int tid) {
    std::pair<K, V> par{key, val};
    auto ret = hash->update(par, tid);
    return optional<V>{};
  };

  ~CLevelHashAdapter() {
    //std::cout << "Removing: " << temp_path() << std::endl;
    //fs::remove(temp_path());
      
    // Wentao: closing the pool results in segfault. Since ycsb test
    // in clevel doesn't close it either, I commented this out here
    // close the pool
    // pool.close();
  }
};

// TODO: allocate an area in which to do work
class CLevelHashFactory : public RideableFactory {
  Rideable* build(GlobalTestConfig *gtc) {
    return new CLevelHashAdapter(gtc);
  }
};

// This macro is defined again in later rideables,
// so I'm preventing it from leaking out
// - Raffi
#undef CAS

#endif /* PMEMOBJ_CLEVEL_HASH_HPP */

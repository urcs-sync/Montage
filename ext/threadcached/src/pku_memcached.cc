#include <pku_memcached.h>
#include <constants.h>
#include <memcached.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef USE_HODOR
#include <hodor.h>
#include <hodor-plib.h>
#endif

extern std::atomic<int> *end_signal;
extern "C" {

static bool run_once = false;

memcached_result_st*
memcached_fetch_result
  (memcached_st *ptr, memcached_result_st *result, memcached_return_t *error){
  return memcached_fetch_result_internal(result, error);
}

char*
memcached_get
  (memcached_st *ptr, const char *key, size_t key_length, size_t *value_length,
   uint32_t *flags, memcached_return_t *error){
  return memcached_get_internal(key, key_length, value_length, flags, error);
}

memcached_return_t
memcached_mget
  (memcached_st *ptr, const char * const *keys, const size_t *key_length, 
   size_t number_of_keys){
  return memcached_mget_internal(keys, key_length, number_of_keys);
}

memcached_return_t
memcached_set
  (memcached_st *ptr, const char * key, size_t nkey, const char * data, size_t datan, 
   uint32_t exptime, uint32_t flags){
  return memcached_set_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_add
  (memcached_st *ptr, const char * key, size_t nkey, const char * data, size_t datan, 
   uint32_t exptime, uint32_t flags){
  return memcached_add_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_replace
  (memcached_st *ptr, const char * key, size_t nkey, const char * data, size_t datan, 
   uint32_t exptime, uint32_t flags){
  return memcached_replace_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_append
  (memcached_st *ptr, const char * key, size_t nkey, const char * data, size_t datan,
   uint32_t exptime, uint32_t flags){
  return memcached_append_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_prepend
  (memcached_st *ptr, const char * key, size_t nkey, const char * data, size_t datan,
   uint32_t exptime, uint32_t flags){
  return memcached_prepend_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_delete 
  (memcached_st *ptr, const char * key, size_t nkey, uint32_t exptime){
  return memcached_delete_internal(key, nkey, exptime);
}

memcached_return_t
memcached_increment
  (memcached_st *ptr, const char * key, size_t nkey, uint64_t delta, uint64_t *value){
  return memcached_increment_internal(key, nkey, delta, value);
}

memcached_return_t
memcached_decrement
  (memcached_st *ptr, const char * key, size_t nkey, uint64_t delta, uint64_t *value){
  return memcached_decrement_internal(key, nkey, delta, value); 
}

memcached_return_t
memcached_increment_with_initial
  (memcached_st *ptr, const char * key, size_t nkey, uint64_t delta, 
   uint64_t initial, uint32_t exptime, uint64_t *value) {
  return memcached_increment_with_initial_internal(key, nkey, delta, initial,
      exptime, value);
}

memcached_return_t
memcached_decrement_with_initial
  (memcached_st *ptr, const char * key, size_t nkey, uint64_t delta, 
   uint64_t initial, uint32_t exptime, uint64_t *value) {
  return memcached_decrement_with_initial_internal(key, nkey, delta, initial, 
      exptime, value);
}

memcached_return_t
memcached_flush(memcached_st *ptr, uint32_t exptime){
  return memcached_flush_internal(exptime);
}

// --------------------- INTERNAL CALLS ------------------------------

item **fetch_ptrs;
static unsigned nptrs = 0;
static unsigned ptrcnt = 0;

// This may not work...
#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_result_st*
memcached_fetch_result_internal
  (memcached_result_st *result, memcached_return_t *error){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  if (nptrs == ptrcnt) {
    *error = MEMCACHED_END;
    return nullptr; 
  }
  item *it = fetch_ptrs[ptrcnt++];
  if (it == nullptr){
    *error = MEMCACHED_NOTFOUND;
    return nullptr;
  }
  if (result == nullptr){
    result = (memcached_result_st*)malloc(sizeof(memcached_result_st));
  }
  result->keyn = ptrcnt;
  result->key = (char*)malloc(it->nkey);
  memcpy(result->key, ITEM_key(it), it->nkey);
  result->key_length = it->nkey;
  result->data = (char*)malloc(it->nbytes);
  memcpy(result->data, ITEM_data(it), it->nbytes);
  result->item_cas = 0;//it->data; // TODO is this right? maybe just default value it
  result->item_flags = it->it_flags;
  result->item_expiration = (time_t)it->exptime;
  return result;
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_fetch_result_internal, 2);
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
char *
memcached_get_internal
  (const char * key, size_t key_length, size_t *value_length, uint32_t *flags,
   memcached_return_t *error){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  *error = MEMCACHED_FAILURE;
  char *buff = NULL;
  *error = pku_memcached_get(key, key_length, buff, value_length,
      flags);
  return buff;
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_get_internal, 5);
#endif


#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_mget_internal
  (const char * const *keys, const size_t *key_length, size_t number_of_keys){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  if (number_of_keys > 128)
    return MEMCACHED_FAILURE;
  pku_memcached_mget(keys, key_length, number_of_keys, fetch_ptrs);
  ptrcnt = 0;
  nptrs = number_of_keys;
  return MEMCACHED_SUCCESS;
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_mget_internal, 3);
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_set_internal
  (const char* key, size_t nkey, const char * data, size_t datan, uint32_t exptime, 
   uint32_t flags){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  auto q = pku_memcached_set(key, nkey, data, datan, exptime);
  return q;
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_set_internal, 6); 
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_add_internal
  (const char* key, size_t nkey, const char * data, size_t datan, uint32_t exptime,
   uint32_t flags){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  return pku_memcached_insert(key, nkey, data, datan, exptime);
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_add_internal, 6);
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_replace_internal
  (const char* key, size_t nkey, const char * data, size_t datan, uint32_t exptime,
   uint32_t flags){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  return pku_memcached_replace(key, nkey, data, datan, exptime, flags);
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_replace_internal, 6);
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_append_internal(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  return pku_memcached_append(key, nkey, data, datan, exptime, flags);
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_append_internal, 6);
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_prepend_internal(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  return pku_memcached_prepend(key, nkey, data, datan, exptime, flags);
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_prepend_internal, 6);
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_delete_internal(const char* key, size_t nkey, uint32_t exptime){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  return pku_memcached_delete(key, nkey, exptime);
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_delete_internal, 3);
#endif


#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_increment_internal
  (const char* key, size_t nkey, uint64_t delta, uint64_t *value){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  switch(add_delta(key, nkey, true, delta, value)){
    case OK:
      return MEMCACHED_SUCCESS;
    default:
      return MEMCACHED_FAILURE;
  }
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_increment_internal, 4)
#endif


#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_decrement_internal
  (const char* key, size_t nkey, uint64_t delta, uint64_t *value){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  switch (add_delta(key, nkey, false, delta, value)){
    case OK:
      return MEMCACHED_SUCCESS;
    default:
      return MEMCACHED_FAILURE;
  }
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_decrement_internal, 4);
#endif


#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_increment_with_initial_internal
  (const char * key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime, 
   uint64_t *value) {
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  char *NT;
  switch(add_delta(key, nkey, true, delta, value)){
    case OK:
      return MEMCACHED_SUCCESS;
    case DELTA_ITEM_NOT_FOUND:
      char buff[32];
      NT = itoa_u64(initial, buff);
      *value = initial;
      return pku_memcached_insert(key, nkey, buff, NT-(&*buff), exptime);
    default:
      return MEMCACHED_FAILURE;
  }
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_increment_with_initial_internal, 6);
#endif


#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_decrement_with_initial_internal
  (const char * key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime, 
   uint64_t *value) {
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  char *NT;
  switch(add_delta(key, nkey, false, delta, value)){
    case OK:
      return MEMCACHED_SUCCESS;
    case DELTA_ITEM_NOT_FOUND:
      char buff[32];
      NT = itoa_u64(initial, buff);
      *value = initial;
      return pku_memcached_insert(key, nkey, buff, NT-(&*buff), exptime);
    default:
      return MEMCACHED_FAILURE;
  }
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_decrement_with_initial_internal, 6);
#endif


#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_flush_internal(uint32_t exptime){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  return pku_memcached_flush(exptime);
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_flush_internal, 1);
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
memcached_return_t
memcached_end(){
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  end_signal->store(1);
  return MEMCACHED_SUCCESS;
} 
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_end, 0);
#endif

#ifdef USE_HODOR
HODOR_FUNC_ATTR
#endif
void
memcached_start_server() {
  assert(run_once && "You must run memcached_init before calling memcached_functions");
  server_thread(nullptr);
}
#ifdef USE_HODOR
HODOR_FUNC_EXPORT(memcached_start_server, 0);
#endif

// Start memcached maintainence processes
// server is either 0 or 1 to represent whether or not we are initializing
// for a server process or a client process
#include <errno.h>
#include <unistd.h>
bool server_flag = false;
void memcached_init(){
  if (!run_once){
    run_once = true;
  } else return;
#ifdef RALLOC
  char* heap_prefix = (char*) malloc(L_cuserid+6);
  cuserid(heap_prefix);
  strcat(heap_prefix, "_memcached");
  is_restart = RP_init(heap_prefix, MEMORY_MAX);
  free(heap_prefix);
#else
  is_restart = pm_init();
#endif
  agnostic_init();
  if (is_restart) {
#ifdef MONTAGE
    assert(0&&"Montage memcached recovery routine not implemented!");
#else
    pm_recover();
#endif
  }
  fetch_ptrs = (item**)pm_malloc(sizeof(item*)*128);
}

#ifdef USE_HODOR
HODOR_INIT_FUNC(memcached_init);
#endif

void
memcached_close() {
  pm_free(fetch_ptrs);
  pm_close();
}

}

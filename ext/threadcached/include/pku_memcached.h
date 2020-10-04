#pragma once
#include <inttypes.h>
#include <pthread.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include "constants.h"

#ifdef __cplusplus
extern "C" {
#endif

memcached_result_st* memcached_fetch_result (
    memcached_st *ptr, memcached_result_st *result, memcached_return_t *error); 
memcached_result_st* memcached_fetch_result_internal (
    memcached_result_st *result, memcached_return_t *error);

char * memcached_get (
    memcached_st *ptr, const char *key, size_t key_length, size_t *value_length,
    uint32_t *flags, memcached_return_t *error); 
char * memcached_get_internal (
    const char *key, size_t key_length, size_t *value_length, uint32_t *flags,
    memcached_return_t *error);

memcached_return_t memcached_mget (
    memcached_st *ptr, const char * const *keys, const size_t *key_length, 
    size_t number_of_keys); 
memcached_return_t memcached_mget_internal (
    const char * const *keys, const size_t *key_length, size_t number_of_keys);

char * memcached_fetch (
    memcached_st *ptr, const char * key, size_t *key_length, 
    size_t *value_length, uint32_t *flags, memcached_return_t *error); 
char * memcached_fetch_internal (const char * key, size_t *key_length, 
    size_t *value_length, uint32_t *flags, memcached_return_t *error);

memcached_return_t memcached_set (memcached_st *ptr, const char * key, 
    size_t nkey, const char * data, size_t datan, uint32_t exptime, 
    uint32_t flags); 
memcached_return_t memcached_set_internal (const char * key, size_t nkey, 
    const char * data, size_t datan, uint32_t exptime, uint32_t flags);

memcached_return_t memcached_add (
    memcached_st *ptr, const char * key, size_t nkey, const char * data, 
    size_t datan, uint32_t exptime, uint32_t flags); 
memcached_return_t memcached_add_internal (
    const char * key, size_t nkey, const char * data, size_t datan, 
    uint32_t exptime, uint32_t flags);

memcached_return_t memcached_replace (
    memcached_st *ptr, const char * key, size_t nkey, const char * data, 
    size_t datan, uint32_t exptime, uint32_t flags); 
memcached_return_t memcached_replace_internal (const char * key, size_t nkey, 
    const char * data, size_t datan, uint32_t exptime, uint32_t flags);

memcached_return_t memcached_prepend (
    memcached_st *ptr, const char * key, size_t nkey, const char * data, 
    size_t datan, uint32_t exptime, uint32_t flags); 
memcached_return_t memcached_prepend_internal (
    const char * key, size_t nkey, const char * data, size_t datan, 
    uint32_t exptime, uint32_t flags);

memcached_return_t memcached_append (
    memcached_st *ptr, const char * key, size_t nkey, const char * data, 
    size_t datan, uint32_t exptime, uint32_t flags); 
memcached_return_t memcached_append_internal (
    const char * key, size_t nkey, const char * data, size_t datan, 
    uint32_t exptime, uint32_t flags);

memcached_return_t memcached_delete (memcached_st *ptr, const char * key, 
    size_t nkey, uint32_t exptime); 
memcached_return_t memcached_delete_internal (const char * key, size_t nkey, 
    uint32_t exptime);

memcached_return_t memcached_increment (memcached_st *ptr, const char * key, 
    size_t nkey, uint64_t delta, uint64_t *value); 
memcached_return_t memcached_increment_internal (
    const char * key, size_t nkey, uint64_t delta, uint64_t *value);

memcached_return_t memcached_decrement (
    memcached_st *ptr, const char * key, size_t nkey, uint64_t delta, 
    uint64_t *value); 
memcached_return_t memcached_decrement_internal (
    const char * key, size_t nkey, uint64_t delta, uint64_t *value);

memcached_return_t memcached_increment_with_initial (
    memcached_st *ptr, const char * key, size_t nkey, uint64_t delta, 
    uint64_t initial, uint32_t exptime, uint64_t *value); 
memcached_return_t memcached_increment_with_initial_internal (
    const char * key, size_t nkey, uint64_t delta, uint64_t initial,
    uint32_t exptime, uint64_t *value);

memcached_return_t memcached_decrement_with_initial (
    memcached_st *ptr, const char * key, size_t nkey, uint64_t delta,
    uint64_t initial, uint32_t exptime, uint64_t *value);
memcached_return_t memcached_decrement_with_initial_internal (
    const char * key, size_t nkey, uint64_t delta, uint64_t initial, 
    uint32_t exptime, uint64_t *value);

memcached_return_t memcached_flush (
    memcached_st *ptr, uint32_t exptime);
memcached_return_t memcached_flush_internal (uint32_t exptime);

void memcached_start_server();

memcached_return_t
memcached_end ();

void memcached_init();

void memcached_close();

#ifdef __cplusplus
}
#endif

// include/hodor_plib.h
// HODOR_FUNC_ATTR
// HODOR_INIT_FUNC
// LOCALDISK, qemu-clea.img

// API that we deprecate
// NOTES: We do not support execute variants because callback functions can not
// be executed from a Hodor context. If they could be, then the security that
// Hodor provisions the user would be overriden. 

#include <assert.h>

#ifdef FAIL_ASSERT
#define FAIL(str) assert(0 && #str);
#elif  FAIL_SILENT
#define FAIL(str) ;
#elif  FAIL_WARNING
#define FAIL(str) fprintf(stderr, #str "\n"); fflush(stderr);
#elif  FAIL_FATAL
#define FAIL(str) ;===;
#else
#error No failure preference given
#endif

// Do not support callbacks because it makes no sense in our context
#undef memcached_mget_execute
#define memcached_mget_execute(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_mget_execute is not supported!);
#undef memcached_mget_execute_by_key
#define memcached_mget_execute_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_mget_execute_by_key is not supported!);
#undef memcached_mget_fetch_execute
#define memcached_mget_fetch_execute(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_mget_fetch_execute is not supported!);
#undef memcached_mget_by_key
#define memcached_mget_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_mget_by_key is not supported!);
#undef memcached_get_by_key
#define memcached_get_by_key(...) \
  NULL;\
  FAIL(memcached_get_by_key is not supported!);
#undef memcached_increment_by_key
#define memcached_increment_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_increment_by_key is not supported!);
#undef memcached_decrement_by_key
#define memcached_decrement_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_decrement_by_key is not supported!);
#undef memcached_increment_with_initial_by_key
#define memcached_increment_with_initial_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_increment_with_initial_by_key is not supported!);
#undef memcached_decrement_with_initial_by_key
#define memcached_decrement_with_initial_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_decrement_with_intial_by_key is not supported!);
#undef memcached_behavior_get
#define memcached_behavior_get(...) \
  0;\
  FAIL(memcached_behavior_get is not supported!);
#undef memcached_behavior_set
#define memcached_behavior_set(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_behavior_set is not supported!);
#undef memcached_callback_set
#define memcached_callback_set(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_callback_set is not supported!);
#undef memcached_callback_get
#define memcached_callback_get(...) \
  NULL;\
  FAIL(memcached_callback_get is not supported!);

// WARNING - should we really be failing silently here???
#undef memcached_create
#define memcached_create(...) NULL
#undef memcached_free
#define memcached_free(...)   ;
#undef memcached_clone
#define memcached_clone(...) NULL
#undef memcached_servers_reset
#define memcached_servers_reset(...) ;

#undef memcached_delete_by_key
#define memcached_delete_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_delete_by_key is not supported!);
#undef memcached_dump
#define memcached_dump(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_dump is not supported!);
#undef memcached_flush_buffers
#define memcached_flush_buffers(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_flush_buffers is not supported!);
#undef memcached_generate_hash_value
#define memcached_generate_hash_value(...) \
  0;\
  FAIL(memcached_generate_hash_value is not supported!);
#undef memcached_generate_hash
#define memcached_generate_hash(...) \
  0;\
  FAIL(memcached_generate_hash is not supported!);
#undef memcached_set_memory_allocators
#define memcached_set_memory_allocators(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_set_memory_allocators is not supported!);
#undef memcached_get_memory_allocators
#define memcached_get_memory_allocators(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_get_memory_allocators is not supported!);
#undef memcached_get_memory_allocators_context
#define memcached_get_memory_allocators_context(...) \
  NULL;\
  FAIL(memcached_get_memory_allocators_context is not supported!);
#undef memcached_pool_create
#define memcached_pool_create(...) \
  NULL;\
  FAIL(memcached_pool_create is not supported!);
#undef memcached_pool_destroy
#define memcached_pool_destroy(...) \
  NULL;\
  FAIL(memcached_pool_destroy is not supported!);
#undef memcached_pool_pop
#define memcached_pool_pop(...) \
  NULL;\
  FAIL(memcached_pool_pop is not supported!);
#undef memcached_pool_push
#define memcached_pool_push(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_pool_push is not supported!);
#undef memcached_pool_behavior_set
#define memcached_pool_behavior_set(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_pool_behavior_set is not supported!);
#undef memcached_pool_behavior_get
#define memcached_pool_behavior_get(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_pool_behavior_get is not supported!);
#undef memcached_quit
#define memcached_quit(...) ;
#undef memcached_server_list_free
#define memcached_server_list_free(...) ;
#undef memcached_server_list_append
#define memcached_server_list_append(...) \
  NULL;\
  FAIL(memcached_server_list_append is not supported!);
#undef memcached_server_list_count
#define memcached_server_list_count(...) \
  0;\
  FAIL(memcached_server_list_count is not supported!);
#undef memcached_servers_parse
#define memcached_servers_parse(...) \
  NULL;\
  FAIL(memcached_servers_parse is not supported!);
#undef memcached_server_error
#define memcached_server_error(...) \
  NULL;\
  FAIL(memcached_ is not supported!);
#undef memcached_server_error_reset
#define memcached_server_error_reset(...) FAIL(memcached_server_error_reset is not supported!);
#undef memcached_server_count
#define memcached_server_count(...) \
  0;\
  FAIL(memcached_server_count is not supported!);
#undef memcached_server_list
#define memcached_server_list(...) \
  NULL;\
  FAIL(memcached_server_list is not supported!);
#undef memcached_server_add
#define memcached_server_add(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_add is not supported!);
#undef memcached_server_add_udp
#define memcached_server_add_udp(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_add_udp is not supported!);
#undef memcached_server_add_unix_socket
#define memcached_server_add_unix_socket(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_add_unix_socket is not supported!);
#undef memcached_server_push
#define memcached_server_push(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_push is not supported!);
#undef memcached_server_push_by_key
#define memcached_server_push_by_key(...) \
  NULL;\
  FAIL(memcached_server_push_by_key is not supported!);
#undef memcached_server_get_last_disconnect
#define memcached_server_get_last_disconnect(...) \
  NULL;\
  FAIL(memcached_server_get_last_disconnect is not supported!);
#undef memcached_server_cursor
#define memcached_server_cursor(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_cursor is not supported!);
#undef memcached_cas
#define memcached_cas(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_cas is not supported!);
#undef memcached_set_by_key
#define memcached_set_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_set_by_key is not supported!);
#undef memcached_add_by_key
#define memcached_add_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_add_by_key is not supported!);
#undef memcached_replace_by_key
#define memcached_replace_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_replace_by_key is not supported!);
#undef memcached_prepend_by_key
#define memcached_prepend_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_prepend_by_key is not supported!);
#undef memcached_append_by_key
#define memcached_append_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_append_by_key is not supported!);
#undef memcached_cas_by_key
#define memcached_cas_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_cas_by_key is not supported!);
// Maybe reenable stats?
#undef memcached_stat
#define memcached_stat(...) \
  NULL;\
  FAIL(memcached_stat is not supported!);
#undef memcached_stat_servername
#define memcached_stat_servername(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_stat_servername is not supported!);
#undef memcached_stat_get_value
#define memcached_stat_get_value(...) \
  NULL;\
  FAIL(memcached_stat_get_value is not supported!);
#undef memcached_stat_get_keys
#define memcached_stat_get_keys(...) \
  NULL;\
  FAIL(memcached_stat_get_keys is not supported!);
#undef memcached_strerror
#define memcached_strerror(...) \
  NULL;\
  FAIL(memcached_strerror is not supported!);
#undef memcached_get_user_data
#define memcached_get_user_data(...) \
  NULL;\
  FAIL(memcached_get_user_data is not supported!);
#undef memcached_set_user_data
#define memcached_set_user_data(...) \
  NULL;\
  FAIL(memcached_set_user_data is not supported!);
#undef memcached_verbosity
#define memcached_verbosity(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_verbosity is not supported!);
#undef memcached_lib_version
#define memcached_lib_version(...) \
  NULL;\
  FAIL(memcached_lib_version is not supported!);
#undef memcached_version
#define memcached_version(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_version is not supported!);

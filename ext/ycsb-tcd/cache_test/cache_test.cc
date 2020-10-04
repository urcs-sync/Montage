#include <pthread.h>
#include <inttypes.h>
#include <pku_memcached.h>
#include <math.h>
#include <string.h>
#include "../core/utils.h"
#include "../core/timer.h"
#include <assert.h>


#define INSERTS (1<<10)
#define READS   1000000

struct ds {
  char tid;
  unsigned successes;
};

#define get_key(tid, n) \
  ((tid << 11) | (n & 0x3FF))

extern unsigned cache_test_item_size;
void * do_requests(void * dat) {
  ds *my_ds = (ds*)dat;
  char tid = my_ds->tid;
  char *vbuff = (char*)malloc(cache_test_item_size);
  memset(vbuff, (char)(rand()), cache_test_item_size-1);
  vbuff[cache_test_item_size-1] = 0;
  
  uint64_t key;
  // insert sets
  for(unsigned i = 0; i < INSERTS; ++i){
    key = get_key(tid, i);
    memcached_set_internal((char*)(&key), sizeof(key), vbuff, cache_test_item_size, 0, 0);
  }
  unsigned succ = 0;

  for(unsigned j = 0; j < READS; ++j) {
    size_t vsz;
    memcached_return_t err;
    uint32_t flags;
    key = get_key(tid, rand());
    memcached_get_internal((char*)&key, sizeof(key), &vsz, &flags, &err);
    succ += err == MEMCACHED_SUCCESS;
  }

  my_ds->successes = succ;
  return NULL;
}

void do_cache_test () {
  if (cache_test_item_size == 0){
    printf("must set cache test item size with '-sz`\n");
    return;
  }
  utils::Timer<double> timer;
  pthread_t threads[8];
  ds datas[8];
  timer.Start();
  for(unsigned i = 0; i < 8; ++i){
    datas[i].tid = i;
    pthread_create(&threads[i], NULL, do_requests, (void*)(&datas[i])); 
  }
  for(unsigned i = 0; i < 8; ++i){
    void* a;
    pthread_join(threads[i], &a);
  }
  double ex = timer.End();
  printf("Cached test completed in %fs\n", ex);

}

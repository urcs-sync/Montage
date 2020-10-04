/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <pku_memcached.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <string.h>

static inline void cpuid(void) {
  asm volatile(
      "cpuid\n\t"
      : ::"%rax", "%rbx", "%rcx", "%rdx");
}

static inline unsigned long get_ticks_start(void) {
  unsigned int cycles_high, cycles_low;
  asm volatile(
      "cpuid\n\t"
      "rdtsc\n\t"
      "mov %%edx, %0\n\t"
      "mov %%eax, %1\n\t"
      : "=r"(cycles_high), "=r"(cycles_low)::"%rax", "%rbx", "%rcx", "%rdx");
  return ((unsigned long)cycles_low) | (((unsigned long)cycles_high) << 32);
}

static inline unsigned long get_ticks_end(void) {
  unsigned int cycles_high, cycles_low;
  asm volatile(
      "rdtscp\n\t"
      "mov %%edx, %0\n\t"
      "mov %%eax, %1\n\t"
      "cpuid\n\t"
      : "=r"(cycles_high), "=r"(cycles_low)::"%rax", "%rbx", "%rcx", "%rdx");
  return ((unsigned long)cycles_low) | (((unsigned long)cycles_high) << 32);
}


#define HMAP_SIZ 600000
#define N_INSERT 100000
#define N_GET    100000
#define BUFF_LEN 64
#define GET_NS(cy, N) ((cy*1.0)/2.2/N)
#define GET_RAND_R(MAX) (rand() % MAX)

char *write_random_string(char* wr){
  for(unsigned i = 0 ;i < BUFF_LEN - 5; ++i)
    wr[i] = rand() | 0x81;
  wr[BUFF_LEN-5] = 0;
  return wr;
}
char *keys[N_INSERT];
char *dats[N_INSERT];

int main(){
  memcached_init();
  std::string name = "chris";
  std::string quality = " tests memcached";

  char nbuff[BUFF_LEN];
  char qbuff[BUFF_LEN];
  strcpy(nbuff, name.c_str());
  strcpy(qbuff, quality.c_str());

  auto t = memcached_set_internal(nbuff, strlen(nbuff), qbuff, strlen(qbuff), 0, 0);
  printf("success? %d\n", t == MEMCACHED_STORED || t == MEMCACHED_SUCCESS);
  memcached_close();
  return 0;
}

/* Copyright (c) 2014 Tim Berning */

#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint64_t round_up(uint64_t num, uint64_t multiple);
char identify_usage(void *ptr);

void clflush(const void *ptr);
void clflush_range(const void *ptr, uint64_t len);

#ifdef HAS_CLFLUSHOPT
void clflushopt(const void *ptr);
void clflushopt_range(const void *ptr, uint64_t len);
#endif

#ifdef HAS_CLWB
void clwb(const void *ptr);
void clwb_range(const void *ptr, uint64_t len);
#endif

void sfence();
void mfence();

/* macros for persistency depending on instruction availability */
#ifdef IMMER_DISABLE_FLUSHING 
    /* Completely disable flushes */
    #define PERSIST(ptr)            do { } while (0)
    #define PERSIST_RANGE(ptr, len) do { } while (0)
#elif HAS_CLWB
    /* CLWB is the preferred instruction, not invalidating any cache lines */
    #define PERSIST(ptr)            do {  clwb(ptr);  } while (0)
    #define PERSIST_RANGE(ptr, len) do {  clwb_range(ptr, len);  } while (0)
#elif HAS_CLFLUSHOPT
    /* CLFLUSHOPT is preferred over CLFLUSH as only dirty cache lines will be evicted */
    #define PERSIST(ptr)            do {  clflushopt(ptr);  } while (0)
    #define PERSIST_RANGE(ptr, len) do {  clflushopt_range(ptr, len); } while (0)
#else
    /* If neither CLWB nor CLFLUSHOPT are available, default to CLFLUSH */
    #define PERSIST(ptr)            do { mfence(); clflush(ptr); mfence(); } while (0)
    #define PERSIST_RANGE(ptr, len) do { mfence(); clflush_range(ptr, len); mfence(); } while (0)
#endif

#endif /* UTIL_H_ */

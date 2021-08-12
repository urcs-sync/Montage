#ifndef NVTRAVERSE_UTILS_HPP
#define NVTRAVERSE_UTILS_HPP

#include <limits.h>
#include <string>
#include "Persistent.hpp"
#include "PString.hpp"

// typedef pds::PString<TESTS_KEY_SIZE> sval_t;
// typedef pds::PString<TESTS_VAL_SIZE> skey_t;

#define CACHE_ALIGN

/*
// #define max(a,b) \
//    ({ __typeof__ (a) _a = (a); \
//        __typeof__ (b) _b = (b); \
//      _a > _b ? _a : _b; })
*/
#define KEY_MAX INT_MAX - 2
#define KEY_MIN 0

#define TRUE 1
#define FALSE 0

#define INF2 (KEY_MAX + 2)
#define INF1 (KEY_MAX + 1)
#define INF0 (KEY_MAX)


typedef uint8_t bool_t;

#if defined(CACHE_ALIGN)
struct alignas(64) node_t : public Persistent{
#else
struct node_t : public Persistent{
#endif
    node_t(){}
    pds::PString<TESTS_KEY_SIZE> key;
    pds::PString<TESTS_VAL_SIZE> value;
    node_t* volatile right;
    node_t* volatile left;
    // uint8_t padding[32];
};

#ifndef __tile__
#ifndef __sparc__

static inline void set_bit(volatile uintptr_t* *array, int bit) {
    asm("bts %1,%0" : "+m" (*array) : "r" (bit));
}
static inline bool_t set_bit2(volatile uintptr_t *array, int bit) {

   // asm("bts %1,%0" :  "+m" (*array): "r" (bit));
     bool_t flag;
     __asm__ __volatile__("lock bts %2,%1; setb %0" : "=q" (flag) : "m" (*array), "r" (bit)); return flag;
   return flag;
}
#endif
#endif


#if defined(CACHE_ALIGN)
struct alignas(64) seek_record_t{
#else
struct seek_record_t{
#endif
	node_t* ancestor;
        node_t* successor;
        node_t* parent;
        node_t* leaf;
 	uint8_t padding[32];
};


static inline uint64_t GETFLAG(volatile node_t* ptr) {
    return ((uint64_t)ptr) & 1;
}

static inline uint64_t GETTAG(volatile node_t* ptr) {
    return ((uint64_t)ptr) & 2;
}

static inline uint64_t FLAG(node_t* ptr) {
    return (((uint64_t)ptr)) | 1;
}

static inline uint64_t TAG(node_t* ptr) {
    return (((uint64_t)ptr)) | 2;
}

static inline uint64_t UNTAG(node_t* ptr) {
    return (((uint64_t)ptr) & 0xfffffffffffffffd);
}

static inline uint64_t UNFLAG(node_t* ptr) {
    return (((uint64_t)ptr) & 0xfffffffffffffffe);
}

static inline node_t* ADDRESS(volatile node_t* ptr) {
    return (node_t*) (((uint64_t)ptr) & 0xfffffffffffffffc);
}
#endif

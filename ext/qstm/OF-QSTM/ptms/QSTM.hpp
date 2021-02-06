/*
 * Copyright 2017-2018
 *   Andreia Correia <andreia.veiga@unine.ch>
 *   Pedro Ramalhete <pramalhe@gmail.com>
 *   Pascal Felber <pascal.felber@unine.ch>
 *   Nachshon Cohen <nachshonc@gmail.com>
 *
 * This work is published under the MIT license. See LICENSE.txt
 */
#ifndef _QSTM_TRANSACTIONAL_MEMORY_WRAPPER_H_
#define _QSTM_TRANSACTIONAL_MEMORY_WRAPPER_H_

#include <atomic>
#include <cassert>
#include <iostream>
#include <vector>
#include <functional>
#include "qstm/src/qstm.hpp"


//#include "qstm/makalu_alloc/include/makalu.h"
#include <fcntl.h>
#include <sys/mman.h>
//#define MAKALU_FILESIZE 4*1024*1024*1024ULL + 24
#define pm_malloc(s) MAK_malloc(s)
#define pm_free(p) MAK_free(p)
#define HEAPFILE "/dev/shm/gc_heap"
static char *curr_addr = NULL;
/*
void __map_persistent_region(){
    int fd; 
    fd  = open(HEAPFILE, O_RDWR | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR);

    off_t offt = lseek(fd, MAKALU_FILESIZE-1, SEEK_SET);
    assert(offt != -1);

    int result = write(fd, "", 1); 
    assert(result != -1);

    void * addr =
        mmap(0, MAKALU_FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); 
    assert(addr != MAP_FAILED);

    *((intptr_t*)addr) = (intptr_t) addr;
    base_addr = (char*) addr;
    //adress to remap to, the root pointer to gc metadata, 
    //and the curr pointer at the end of the day
    curr_addr = (char*) ((size_t)addr + 3 * sizeof(intptr_t));
    printf("Addr: %p\n", addr);
    printf("Base_addr: %p\n", base_addr);
    printf("Current_addr: %p\n", curr_addr);
}
int __nvm_region_allocator(void** memptr, size_t alignment, size_t size)
{   
    char* next;
    char* res; 
    
    if (((alignment & (~alignment + 1)) != alignment)  ||   //should be multiple of 2
        (alignment < sizeof(void*))) return 1; //should be atleast the size of void*
    size_t aln_adj = (size_t) curr_addr & (alignment - 1);
    
    if (aln_adj != 0)
        curr_addr += (alignment - aln_adj);
    
    res = curr_addr; 
    next = curr_addr + size;
    if (next > base_addr + MAKALU_FILESIZE){
        printf("\n----Ran out of space in mmaped file-----\n");
        return 1;
    }
    curr_addr = next;
    *memptr = res;
    //printf("Current NVM Region Addr: %p\n", curr_addr);
    
    return 0;
}
*/
#define pm_init() {\
  __map_persistent_region();\
MAK_start(&__nvm_region_allocator);\
}

#define pm_close() MAK_close()

#define pm_pthread_create (a, b, c, d) MAK_pthread_create (a, b, c, d)


__thread QSTM* txn = nullptr; // Per-thread object
TestConfig* tc;
QSTMFactory* stm_factory;

//namespace qstm {

// Compile with explicit calls to TinySTM
//#include "tinystm/include/stm.h"
//#include "tinystm/include/mod_mem.h"
//#include "tinystm/include/mod_ab.h"



struct tmbase {
};


//
// Thread Registry stuff
//
extern void thread_registry_deregister_thread(const int tid);

// An helper class to do the checkin and checkout of the thread registry
struct ThreadCheckInCheckOut {
    static const int NOT_ASSIGNED = -1;
    int tid { NOT_ASSIGNED };
    ~ThreadCheckInCheckOut() {
        if (tid == NOT_ASSIGNED) return;
        thread_registry_deregister_thread(tid);
    }
};

extern thread_local ThreadCheckInCheckOut tl_gc_tcico;

// Forward declaration of global/singleton instance
class ThreadRegistry;
extern ThreadRegistry gThreadRegistry;

/*
 * <h1> Registry for threads </h1>
 *
 * This is singleton type class that allows assignement of a unique id to each thread.
 * The first time a thread calls ThreadRegistry::getTID() it will allocate a free slot in 'usedTID[]'.
 * This tid wil be saved in a thread-local variable of the type ThreadCheckInCheckOut which
 * upon destruction of the thread will call the destructor of ThreadCheckInCheckOut and free the
 * corresponding slot to be used by a later thread.
 * RomulusLR relies on this to work properly.
 */
class ThreadRegistry {
public:
    static const int                    REGISTRY_MAX_THREADS = 128;

private:
    alignas(128) std::atomic<bool>      usedTID[REGISTRY_MAX_THREADS];   // Which TIDs are in use by threads
    alignas(128) std::atomic<int>       maxTid {-1};                     // Highest TID (+1) in use by threads

public:
    ThreadRegistry() {
        for (int it = 0; it < REGISTRY_MAX_THREADS; it++) {
            usedTID[it].store(false, std::memory_order_relaxed);
        }
    }

    // Progress Condition: wait-free bounded (by the number of threads)
    int register_thread_new(void) {
        for (int tid = 0; tid < REGISTRY_MAX_THREADS; tid++) {
            if (usedTID[tid].load(std::memory_order_acquire)) continue;
            bool unused = false;
            if (!usedTID[tid].compare_exchange_strong(unused, true)) continue;
            // Increase the current maximum to cover our thread id
            int curMax = maxTid.load();
            while (curMax <= tid) {
                maxTid.compare_exchange_strong(curMax, tid+1);
                curMax = maxTid.load();
            }
            tl_gc_tcico.tid = tid;
            return tid;
        }
        std::cout << "ERROR: Too many threads, registry can only hold " << REGISTRY_MAX_THREADS << " threads\n";
        assert(false);
    }

    // Progress condition: wait-free population oblivious
    inline void deregister_thread(const int tid) {
//        stm_exit_thread();    // Needed by TinySTM
        usedTID[tid].store(false, std::memory_order_release);
    }

    // Progress condition: wait-free population oblivious
    static inline uint64_t getMaxThreads(void) {
        return gThreadRegistry.maxTid.load(std::memory_order_acquire);
    }

    // Progress condition: wait-free bounded (by the number of threads)
    static inline int getTID(void) {
        int tid = tl_gc_tcico.tid;
        if (tid != ThreadCheckInCheckOut::NOT_ASSIGNED) return tid;
//        stm_init_thread();   // Needed by TinySTM
        return gThreadRegistry.register_thread_new();
    }
};


class TinySTM;
extern TinySTM gTinySTM;

class TinySTM {

private:
    // Maximum number of participating threads
    static const uint64_t MAX_THREADS = 128;

public:
    TinySTM(unsigned int maxThreads=MAX_THREADS) {
//		__map_persistent_region();
//		MAK_start(&__nvm_region_allocator);
		RP_init("test", 10737418240);
std::cout << "ralloc init\n";
		QSTM::init(); // MAX_THREADS is ignored, 128 is hardcoded
		tc = new TestConfig();
		stm_factory = tc->getTMFactory();
//        stm_init();
//        mod_mem_init(0);
//        mod_ab_init(0, NULL);
    }

    ~TinySTM() {
//        stm_exit();
		if(txn == nullptr) txn = tc->getTMFactory()->build(tc, 999);
		txn->gc_worker();
		free(txn);
    }

//    static std::string qclassName() { return "QSTM"; }

    template<typename R, class F>
    static R updateTx(F&& func) {
        const unsigned int tid = ThreadRegistry::getTID();
        R retval{};
		if(txn == nullptr) txn = tc->getTMFactory()->build(tc, tid);
		// TODO set CPU affinity here
		if(txn->nesting == 0) setjmp(txn->env);
        txn->tm_begin();
//        stm_tx_attr_t _a = {{.id = (unsigned int)tid, .read_only = false}};
//        sigjmp_buf *_e = stm_start(_a);
//        sigsetjmp(*_e, 0);
        retval = func();
        txn->tm_end();
        return retval;
    }

    template<class F>
    static void updateTx(F&& func) {
        const unsigned int tid = ThreadRegistry::getTID();
		if(txn == nullptr) txn = tc->getTMFactory()->build(tc, tid);
		// TODO set CPU affinity here
		if(txn->nesting == 0) setjmp(txn->env);
        txn->tm_begin();
//        stm_tx_attr_t _a = {{.id = (unsigned int)tid, .read_only = false}};
//        sigjmp_buf *_e = stm_start(_a);
//        sigsetjmp(*_e, 0);
        func();
        txn->tm_end();
    }

    template<typename R, class F>
    static R readTx(F&& func) {
        const unsigned int tid = ThreadRegistry::getTID();
        R retval{};
		if(txn == nullptr) txn = tc->getTMFactory()->build(tc, tid);
		// TODO set CPU affinity here
		if(txn->nesting == 0) setjmp(txn->env);
        txn->tm_begin();
//        stm_tx_attr_t _a = {{.id = (unsigned int)tid, .read_only = true}};
//        sigjmp_buf *_e = stm_start(_a);
//        sigsetjmp(*_e, 0);
        retval = func();
        txn->tm_end();
        return retval;
    }

    template<class F>
    static void readTx(F&& func) {
        const int tid = ThreadRegistry::getTID();
		if(txn == nullptr) txn = tc->getTMFactory()->build(tc, tid);
		// TODO set CPU affinity here
		if(txn->nesting == 0) setjmp(txn->env);
        txn->tm_begin();
//        stm_tx_attr_t _a = {{.id = (unsigned int)tid, .read_only = true}};
//        sigjmp_buf *_e = stm_start(_a);
//        sigsetjmp(*_e, 0);
        func();
        txn->tm_end();
    }

    template <typename T, typename... Args>
    static T* tmNew(Args&&... args) {
        void* addr = txn->tm_malloc(sizeof(T));
        assert(addr != NULL);
        T* ptr = new (addr) T(std::forward<Args>(args)...);
        return ptr;
    }

//    template<typename T>
//    static void tmDelete(T* obj) {
//        if (obj == nullptr) return;
//        obj->~T();
//        txn->tm_free(obj, sizeof(T));
//        txn->tm_free(obj); // FIXME What is going on with that second arg? What does TinySTM do with it?
//    }

    static void* tmMalloc(size_t size) {
        return txn->tm_malloc(size);
    }

    static void tmFree(void* obj) {
//        txn->tm_free(obj, 0);
        txn->tm_free((void **)obj); // FIXME What is going on with that second arg? What does TinySTM do with it?
    }
};



// T is typically a pointer to a node, but it can be integers or other stuff, as long as it fits in 64 bits
template<typename T>
struct tmtype {
    T val {};

    tmtype() { }

    tmtype(T initVal) : val{initVal} {}

    // Casting operator
    operator T() {
        return load();
    }

    // Prefix increment operator: ++x
    void operator++ () {
        store(load()+1);
    }

    // Prefix decrement operator: --x
    void operator-- () {
        store(load()-1);
    }

    void operator++ (int) {
        store(load()+1);
    }

    void operator-- (int) {
        store(load()-1);
    }

    // Equals operator: first downcast to T and then compare
    bool operator == (const T& otherval) const {
        return load() == otherval;
    }

    // Difference operator: first downcast to T and then compare
    bool operator != (const T& otherval) const {
        return load() != otherval;
    }

    // Operator arrow ->
    T operator->() {
        return load();
    }

    // Copy constructor
    tmtype<T>(const tmtype<T>& other) {
        store(other.load());
    }

    // Assignment operator from an tmtype
    tmtype<T>& operator=(const tmtype<T>& other) {
        store(other.load());
        return *this;
    }

    // Assignment operator from a value
    tmtype<T>& operator=(T value) {
        store(value);
        return *this;
    }

    inline void store(T newVal) {
        //txn->tm_write((uintptr_t *)&val, (uintptr_t)newVal);
        txn->tm_write((void**)&val, (void*)newVal);
    }

    // Meant to be called when know we're the only ones touching
    // these contents, for example, in the constructor of an object, before
    // making the object visible to other threads.
    inline void isolated_store(T newVal) {
        val = newVal;
    }

    inline T load() const {
//        return (T)stm_load((uintptr_t *)&val);
		return (T)(txn->tm_read((void**)&val));
    }
};

extern TinySTM gTinySTM;


// Wrapper to not do any transaction
template<typename R, typename Func>
R notx(Func&& func) {
    return func();
}

template<typename R, typename F> static R updateTx(F&& func) { return gTinySTM.updateTx<R>(func); }
template<typename R, typename F> static R readTx(F&& func) { return gTinySTM.readTx<R>(func); }
template<typename F> static void updateTx(F&& func) { gTinySTM.updateTx(func); }
template<typename F> static void readTx(F&& func) { gTinySTM.readTx(func); }
template<typename T, typename... Args> T* tmNew(Args&&... args) { return gTinySTM.tmNew<T>(args...); }
//template<typename T>void tmDelete(T* obj) { gTinySTM.tmDelete<T>(obj); }
static void* tmMalloc(size_t size) { return TinySTM::tmMalloc(size); }
static void tmFree(void* obj) { TinySTM::tmFree(obj); }

static int getTID(void) { return ThreadRegistry::getTID(); }


//
// Place these in a .cpp if you include this header in multiple files
//
extern char* base_addr;
TinySTM gTinySTM {};
// Global/singleton to hold all the thread registry functionality
ThreadRegistry gThreadRegistry {};
// This is where every thread stores the tid it has been assigned when it calls getTID() for the first time.
// When the thread dies, the destructor of ThreadCheckInCheckOut will be called and de-register the thread.
thread_local ThreadCheckInCheckOut tl_gc_tcico {};
// Helper function for thread de-registration
void thread_registry_deregister_thread(const int tid) {
    gThreadRegistry.deregister_thread(tid);
}



//}

#endif /* _QSTM_TRANSACTIONAL_MEMORY_WRAPPER_H_ */

#pragma once

#include <pthread.h>
#include <stdarg.h>
#include "savitar.hpp"

typedef struct NvMethodCall {
    uint64_t obj_ptr;
    uint64_t method_tag;
    uint64_t arg_ptrs[BUFFER_SIZE - 2];
} NvMethodCall;

typedef struct TxBuffers {
    NvMethodCall *buffer;
    uint64_t *tx_buffer;
    int thread_id; // pthread_self() for main thread
} TxBuffers;

typedef struct ThreadConfig {
    int core_id;
    NvMethodCall *buffer;
    uint64_t *tx_buffer;
    void *(*routine)(void *);
    void *argument;
} ThreadConfig;

void Savitar_core_init();
void Savitar_core_finalize();

int Savitar_thread_create(pthread_t *, const pthread_attr_t *,
    void *(*start_routine)(void *), void *);

/*
 * The main thread communicates with the logger thread through
 * the thread_notify function. Here is the list and order of
 * arguments:
 * thread_notify(logger_func, log, arg_1, ..., arg_n)
 */
void Savitar_thread_notify(int, ...);

/*
 * The main thread waits for the logger thread to finish logging
 * through calling this function.
 */
void Savitar_thread_wait(PersistentObject *, SavitarLog *log);

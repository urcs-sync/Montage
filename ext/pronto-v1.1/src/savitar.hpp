#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "nv_object.hpp"
#include "nvm_manager.hpp"
#include "nv_factory.hpp"
#include "stl_alloc.hpp"

#define MAX_THREADS                 40
#define CACHE_LINE_WIDTH            64
#define BUFFER_SIZE                 8
#define MAX_CORES                   80
#ifndef PRONTO_BUF
#define MAX_ACTIVE_TXS              15
#else
#define MAX_ACTIVE_TXS              ((size_t) 1 << 10) // 1024
#endif
#define CATALOG_FILE_NAME           "savitar.cat"
#define CATALOG_FILE_SIZE           ((size_t)8 << 20) // 8 MB
#define CATALOG_HEADER_SIZE         ((size_t)2 << 20) // 2 MB
#define PMEM_PATH                   "/mnt/pmem/"
#ifndef LOG_SIZE
#define LOG_SIZE                    ((off_t)1 << 30) // 1 GB
#endif
#define NESTED_TX_TAG               0x8000000000000000
#define REDO_LOG_MAGIC              0x5265646F4C6F6745 // RedoLogE

#ifdef DEBUG
#define PRINT(format, ...)          fprintf(stdout, format, ## __VA_ARGS__)
#else
#define PRINT(format, ...)          {}
#endif

typedef struct CatalogEntry {
    uuid_t uuid;
    uint64_t type;
    uint64_t args_offset; // Catalog offset for constructor arguments
} CatalogEntry;

typedef int (*MainFunction)(int, char **);

int Savitar_main(MainFunction, int, char **);

int Savitar_thread_create(pthread_t *, const pthread_attr_t *,
    void *(*start_routine)(void *), void *);

void Savitar_thread_notify(int, ...);

void Savitar_thread_wait(PersistentObject *, SavitarLog *);

#ifdef PRONTO_BUF
int tx_buffer_empty();
#endif

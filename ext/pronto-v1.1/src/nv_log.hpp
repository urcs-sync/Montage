#pragma once
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

/*
 * checksum: to check if the log is initialized
 * object_id: uuid of persistent object corresponding to the log
 * size: log size including the header
 * head/tail: offset of entries from the beginning of mapped region
 * last_commit: does not need to be persistent (can be recovered from entries)
 */
typedef struct RedoLog {
    uint64_t checksum;
    uuid_t object_id;
    uint64_t size;
    uint64_t head;
    uint64_t tail;
    uint64_t last_commit;
    uint64_t snapshot_lock; // temporary value
} SavitarLog;

typedef struct SavitarVector {
    void *addr;
    size_t len;
} ArgVector;

SavitarLog *Savitar_log_open(uuid_t);
SavitarLog *Savitar_log_create(uuid_t, size_t);
void Savitar_log_close(SavitarLog *);

bool Savitar_log_exists(uuid_t);
uint64_t Savitar_log_append(SavitarLog *, ArgVector *, size_t);
void Savitar_log_commit(SavitarLog *, uint64_t);

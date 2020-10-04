#include <stdio.h>
#include "persister.hpp"
#include "nv_object.hpp"
#include "thread.hpp"

#ifdef DEBUG
static inline uint64_t rdtscp() {
  uint32_t aux;
  uint64_t rax, rdx;
  asm volatile ( "rdtscp\n" : "=a" (rax), "=d" (rdx), "=c" (aux) : : );
  return (rdx << 32) + rax;
}
#endif

void *Savitar_persister_worker(void *arg) {

    NvMethodCall *buffer = ((TxBuffers *)arg)->buffer;
    uint64_t *tx_buffer = ((TxBuffers *)arg)->tx_buffer;
    int thread_id = ((TxBuffers *)arg)->thread_id;
    PersistentObject *nv_object = NULL;

    /*
     * [Support for nested transactions]
     * Transaction ID for the first uncommitted transaction
     * Possible values are 0 to MAX_ACTIVE_TXS - 1
     * method_tag != 0: continue with persisting the log entry
     * method_tag == 0: let A = active_tx_id and B = tx_buffer[0]
     * * B == 0: no active transaction, keep waiting (set A = 0)
     * * A == B - 1: logged transaction still in progress, keep waiting
     * * A > B - 1: set A = A - 1 as the last logged transaction is now committed
     * * A < B - 1: set A = A + 1 as there are pending transactions
     */
    uint64_t active_tx_id = 0;

    while (true) {
        // Wait for main thread to setup buffer
        while (buffer[active_tx_id].method_tag == 0) {
            if (tx_buffer[0] == 0) {
                active_tx_id = 0;
                continue;
            }
            if (active_tx_id > tx_buffer[0] - 1) active_tx_id--;
            else if (active_tx_id < tx_buffer[0] - 1) active_tx_id++;
        }

        // Check for TERM signal from main thread
        if (buffer[active_tx_id].method_tag == UINT64_MAX) {
            PRINT("[%d] Received TERM signal from the main thread\n", thread_id);
            break;
        }
#ifdef DEBUG
        uint64_t cycle = rdtscp();
#endif

        // Extract PersistentObject pointer and method tag
        nv_object = (PersistentObject *)buffer[active_tx_id].obj_ptr;

        uint64_t log_offset;
        if (active_tx_id > 0) { // dependant (nested) transaction
            if (tx_buffer[active_tx_id] == 0) {
                // we must first create undo-log for parent transaction
                active_tx_id--;
                continue;
            }
            ArgVector vector[2];
            uint64_t nested_tx_tag = tx_buffer[active_tx_id] | NESTED_TX_TAG;
            vector[0].addr = &nested_tx_tag;
            vector[0].len = sizeof(nested_tx_tag);
            vector[1].addr = ((PersistentObject *)buffer[active_tx_id - 1].obj_ptr)->getUUID();
            vector[1].len = sizeof(uuid_t);
            log_offset = nv_object->AppendLog(vector, 2);
#ifdef DEBUG
            char parent_uuid_str[64];
            uuid_unparse(((PersistentObject *)buffer[active_tx_id - 1].obj_ptr)->getUUID(),
                    parent_uuid_str);
            PRINT("[%d] Creating dependant log with parent uuid = %s\n",
                    thread_id, parent_uuid_str);
#endif
        }
        else { // outer-most transaction
            // Delegate log creation to the logger function
            log_offset = nv_object->Log(buffer[active_tx_id].method_tag,
                    buffer[active_tx_id].arg_ptrs);
        }
        tx_buffer[active_tx_id + 1] = log_offset;

#ifdef DEBUG
        buffer[active_tx_id].arg_ptrs[1] = rdtscp();
        buffer[active_tx_id].arg_ptrs[0] = cycle;
        char object_uuid_str[64];
        uuid_unparse(nv_object->getUUID(), object_uuid_str);
        PRINT("[%d] Created semantic log for %s at offset %zu.\n",
                thread_id, object_uuid_str, tx_buffer[active_tx_id + 1]);
#endif
        // Notify main thread
        asm volatile("mfence" : : : "memory");
        buffer[active_tx_id].method_tag = 0;
    }

    return NULL;
}

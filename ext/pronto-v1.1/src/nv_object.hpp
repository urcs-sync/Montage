#pragma once

#include <uuid/uuid.h>
#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include "nv_log.hpp"
#include "nvm_manager.hpp"
#include "ckpt_alloc.hpp"

class NVManager;
class Snapshot;

/*
 * Objects demanding transactional durability must extend this class and
 * implement abstract methods. The state of the object is recovered using
 * its 'uuid', provided to the object constructor.
 */
class PersistentObject {
    public:
        PersistentObject(bool dummy = false);
        PersistentObject(uuid_t);
        ~PersistentObject();

        /*
         * Called by persister threads upon processing sync_buffers
         * Returns the offset of log entry
         */
        virtual uint64_t Log(uint64_t, uint64_t *) = 0;

        inline uint64_t AppendLog(ArgVector *vector, int v_size) {
            return Savitar_log_append(this->log, vector, v_size);
        }

        inline unsigned char *getUUID() const {
            return (unsigned char *)uuid;
        }

        bool isRecovering() { return recovering != 0; }
        bool isWaitingForSnapshot() { return log->snapshot_lock != 0; }

        ObjectAlloc *getAllocator() { return alloc; }

        // TODO support for permanent deletes
        void operator delete (void *ptr) {
            PersistentObject *obj = (PersistentObject *)ptr;
            if (obj->log == NULL) return;
            fprintf(stdout, "Deleting persistent object: %s\n",
                    obj->uuid_str);
            NVManager &manager = NVManager::getInstance();
            manager.lock();
            manager.destroy(obj);
            manager.unlock();
        }

    private:
        /*
         * Back-end support for isWaitingForParent()
         * This method is called by Recover() for nested transactions.
         */
        PersistentObject *wait_parent = NULL;
        uint64_t wait_parent_commit_id = 0;
        void waitForParent(PersistentObject *parent, uint64_t commit_id) {
            wait_parent = parent;
            wait_parent_commit_id = commit_id;
        }

        void constructor(uuid_t id);

    public:
        /*
         * Part of the recovery process for nested transaction
         * Checks if this object is waiting for the provided object
         * and if the current log offset of the provided object
         * matches what this object has recorded in its semantic log.
         */
        bool isWaitingForParent(PersistentObject *object) {
            if (object != wait_parent) return false;
            if (object->last_played_commit_id + 1 != wait_parent_commit_id)
                return false;
            return true;
        }

    protected:
        // Called by NVM Manager during the recovery process
        void Recover();

        // Called by the NVM Manager through Recover()
        virtual size_t Play(uint64_t tag, uint64_t *args, bool dry) = 0;

        /*
         * Constructor arguments buffer
         * Filled by the constructor method of child objects.
         * Multiple constructors is handled by the factory method (TODO).
         * During recovery, the constructor will skip setting these fields.
         * After returning from the constructor, NVM Manager will pass these
         * attributes to NVCatalog, which will then persist them on NVM.
         * During recovery, the factory method of the object is responsible
         * for de-serializing the persisted arguments and recreating the object.
         */
        void *const_args = NULL;
        size_t const_args_size = 0;

        struct { // 16 + 38 + 1 + 1 + 8 = 64 bytes
            uuid_t uuid;
            char uuid_str[38];
            // object is assigned to a live variable?
            uint8_t assigned = 0;
            uint8_t recovering = 0;

            SavitarLog *log = NULL;
        };

        // commit id of the last played log entry
        uint64_t last_played_commit_id;
        ObjectAlloc *alloc = NULL;

        friend class NVManager;
        friend class Snapshot;
};

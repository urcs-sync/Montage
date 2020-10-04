#pragma once
#include <pthread.h>
#include <uuid/uuid.h>
#include <cstring>
#include <assert.h>
#include <string>
#include <map>

using namespace std;

class NVManager;

class RecoveryContext {
    public:
        RecoveryContext() {
            pthread_mutex_init(&lock, NULL);
        }

        ~RecoveryContext() {
            pthread_mutex_destroy(&lock);
            parentObjects.clear();
            logHeadOffsets.clear();
        }

        static RecoveryContext& getInstance() {
            static RecoveryContext instance;
            return instance;
        }

        void setManager(NVManager *m) { manager = m; }
        NVManager *getManager() { return manager; }

        /*
         * Support for recovering nested transactions
         * Pop returns the caller object (NULL means non-nested Tx)
         * Push sets parent object to the provided Persistent Object
         */
        void pushParentObject(PersistentObject *parent) {
            pthread_mutex_lock(&lock);
            auto it = parentObjects.find(pthread_self());
            assert(it == parentObjects.end());
            parentObjects[pthread_self()] = parent;
            pthread_mutex_unlock(&lock);
        }

        PersistentObject *popParentObject() {
            PersistentObject *parent = NULL;
            pthread_mutex_lock(&lock);
            auto it = parentObjects.find(pthread_self());
            if (it != parentObjects.end()) {
                parent = it->second;
                parentObjects.erase(it);
            }
            pthread_mutex_unlock(&lock);
            return parent;
        }

        void pushLogHeadOffset(string id, uint64_t head) {
            logHeadOffsets.insert(pair<string, uint64_t>(id, head));
        }

        uint64_t queryLogHeadOffset(string id) {
            auto it = logHeadOffsets.find(id);
            if (it == logHeadOffsets.end()) return 0;
            return it->second;
        }

    private:
        NVManager *manager = NULL;
        map<pthread_t, PersistentObject *> parentObjects;
        pthread_mutex_t lock;
        map<string, uint64_t> logHeadOffsets;
};

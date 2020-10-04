#pragma once
#include <map>
#include <pthread.h>

using namespace std;
class NVCatalog;
class PersistentObject;
struct CatalogEntry;
struct ThreadConfig;
class Snapshot;

/*
 * Non-Volatile Memory Manager
 * * Handles creation, recovery and destruction of persistent objects.
 * * Maintains a list of existing persistent objects (i.e., catalog).
 * * Spawns recovery threads on startup to recover all persistent objects.
 */
class NVManager {
    public:
        NVManager();
        // Destructor: handles clean shutdown
        ~NVManager();

        // Singleton interface
        static NVManager& getInstance() {
            static NVManager instance;
            return instance;
        }

        void lock() { pthread_mutex_lock(&_lock); }
        void unlock() { pthread_mutex_unlock(&_lock); }

        /*
         * Create a new or find an existing persistent object
         * Find: tries to find an already recovered object
         * Create: saves the newly created object in the catalog
         */
        PersistentObject *findRecovered(uuid_t);
        void createNew(uint64_t, PersistentObject *);

        // Handles 'delete' for persistent objects
        void destroy(PersistentObject *);

        // Find pointer to persistent objects using its unique identifier
        PersistentObject *findObject(string);

        const char *getArgumentPointer(CatalogEntry *);

        pthread_cond_t *ckptCondition() { return &_ckptCondition; }
        pthread_mutex_t *ckptLock() { return &_ckptLock; }

        void registerThread(pthread_t, ThreadConfig *);
        void unregisterThread(pthread_t);

    private:
        pthread_mutex_t _lock;
        pthread_mutex_t _ckptLock;
        pthread_cond_t _ckptCondition;
        map<string, PersistentObject *> objects;
        NVCatalog *catalog = NULL;
        map<pthread_t, ThreadConfig *> program_threads;

        /*
         * Recovery methods
         * recoverObject: Catalog calls this method to add objects to recovery queue
         * recoveryWorker: for each object in the recovery queue, a new thread is
         * created, which calls this method to run the recovery code for its
         * argument object.
         */
        void recoverObject(const char *, struct CatalogEntry *);
        static void *recoveryWorker(void *);

        /*
         * Method to fix persistent log dependencies after an unclean shutdown
         */
        static void *buildAbortChains(void *);
        int emulateRecoveryAndFixRedoLogs();

        friend class Snapshot;
};

#pragma once
#include <map>

using namespace std;

typedef struct CatalogEntry CatalogEntry;
class PersistentObject;
class NVManager;
typedef PersistentObject *(*FactoryMethod)(NVManager *, CatalogEntry *);

class PersistentFactory {
    public:
        PersistentFactory();
        ~PersistentFactory();

        static PersistentObject *create(NVManager *, uint64_t, CatalogEntry *);

        /*
         * Registers factory methods of persistent objects with
         * their unique type identifiers.
         */
        static void registerFactory(uint64_t type_id, FactoryMethod method_ptr,
                uintptr_t vTable_ptr);

        template <class T>
        static void registerFactory() {
            uint64_t type_id = T::classID();
            FactoryMethod method_ptr = T::RecoveryFactory;
            T *t = new T();
            uintptr_t vTable_ptr = ((uintptr_t*)t)[0];
            delete t;
            registerFactory(type_id, method_ptr, vTable_ptr);
        }

        static void vTableUpdate(uint64_t type_id, PersistentObject *obj_ptr);
    private:
        /*
         * Map between class type identifiers and factory functions
         * We use this map to implement reflection (dynamic object allocation)
         */
        map<uint64_t, FactoryMethod> factory_map;

        /*
         * Map between class type identifiers and vTable pointers
         * We use this to handle ASLR when recovering from snapshots
         */
        map<uint64_t, uintptr_t> vTable_map;

        static PersistentFactory& singleton();
};

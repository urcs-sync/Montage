#include <assert.h>
#include "nv_factory.hpp"

PersistentFactory::PersistentFactory() {  }

PersistentFactory::~PersistentFactory() {
    factory_map.clear();
}

PersistentFactory& PersistentFactory::singleton() {
    static PersistentFactory instance;
    return instance;
}

PersistentObject *PersistentFactory::create(NVManager *manager,
        uint64_t type_id, CatalogEntry *entry) {
    PersistentFactory& me = singleton();
    auto it = me.factory_map.find(type_id);
    assert(it != me.factory_map.end());
    return it->second(manager, entry);
}

void PersistentFactory::registerFactory(uint64_t type_id,
        FactoryMethod method_ptr, uintptr_t vTable_ptr) {
    PersistentFactory& me = singleton();
    assert(me.factory_map.find(type_id) == me.factory_map.end());
    me.factory_map.insert(pair<uint64_t, FactoryMethod>(type_id, method_ptr));
    me.vTable_map.insert(pair<uint64_t, uintptr_t>(type_id, vTable_ptr));
}

void PersistentFactory::vTableUpdate(uint64_t type_id, PersistentObject *obj_ptr) {
    PersistentFactory& me = singleton();
    assert(me.vTable_map.find(type_id) != me.vTable_map.end());
    ((uintptr_t*)obj_ptr)[0] = me.vTable_map[type_id];
}

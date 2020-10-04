#include <assert.h>
#include <string.h>
#include "nv_catalog.hpp"
#include "savitar.hpp"
#include "nvm_manager.hpp"

using namespace std;

NVCatalog::NVCatalog(string path, list< pair<std::string, CatalogEntry *> >& objects) {
    char catalog_path[255];
    strcpy(catalog_path, PMEM_PATH);
    strcat(catalog_path, path.c_str());

    size_t mapped_len;
    catalog = (Catalog *)pmem_map_file(catalog_path, 0, 0, 0,
            &mapped_len, NULL);
    if (catalog != NULL) { // open existing catalog
        assert(mapped_len == CatalogSize);
        assert(catalog->magic == CatalogMagic);
        PRINT("Opening existing catalog, total objects = %zu\n",
                catalog->object_count);

        for (uint64_t i = 0; i < catalog->object_count; i++) {
            char uuid_str[64];
            uuid_unparse(catalog->objects[i].uuid, uuid_str);
            PRINT("Catalog found object with uuid = %s, type = %zu, arges_offset = %zu\n",
                    uuid_str, catalog->objects[i].type, catalog->objects[i].args_offset);
            objects.push_back(pair<std::string, CatalogEntry *>(uuid_str,
                        &catalog->objects[i]));
        }
    }
    else { // create new catalog
        PRINT("Creating new catalog ...\n");
        catalog = (Catalog *)pmem_map_file(catalog_path, CatalogSize,
                PMEM_FILE_CREATE | PMEM_FILE_EXCL, 0666, &mapped_len, NULL);
        assert(catalog != NULL);
        memset(catalog, 0, CACHE_LINE_WIDTH);
        catalog->free_offset = CATALOG_HEADER_SIZE;
        pmem_persist(catalog, CACHE_LINE_WIDTH);
        catalog->magic = CatalogMagic;
        pmem_persist(catalog, CACHE_LINE_WIDTH);
    }
}

NVCatalog::~NVCatalog() {
    pmem_unmap(catalog, CatalogSize);
}

CatalogEntry *NVCatalog::add(uuid_t uuid, uint64_t type) {
    uint64_t offset = catalog->object_count;
    uuid_copy(catalog->objects[offset].uuid, uuid);
    catalog->objects[offset].type = type;
    pmem_persist(&catalog->objects[offset], sizeof(CatalogEntry));
    catalog->object_count++;
    pmem_persist(catalog, CACHE_LINE_WIDTH);
    return &catalog->objects[offset];
}

void NVCatalog::addConstructorArgs(CatalogEntry *obj, void *buffer,
        size_t size) {
    uint64_t offset = __sync_fetch_and_add(&catalog->free_offset, size);
    assert(offset + size <= CATALOG_FILE_SIZE);
    pmem_persist(catalog, CACHE_LINE_WIDTH);
    char *dst = (char *)catalog + offset;
    pmem_memcpy_persist(dst, buffer, size);
    obj->args_offset = offset;
    pmem_persist(obj, sizeof(CatalogEntry));
}

void NVCatalog::setFlags(uint64_t flags) {
    catalog->flags = flags;
    pmem_persist(catalog, CACHE_LINE_WIDTH);
}

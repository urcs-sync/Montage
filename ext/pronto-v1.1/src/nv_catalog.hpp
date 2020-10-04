#pragma once
#include <libpmem.h>
#include <uuid/uuid.h>
#include <type_traits>
#include <string>
#include <stdint.h>
#include <list>
#include "savitar.hpp"

using namespace std;

const uint64_t CatalogMagic = 0x434154414C4F4700; // CATALOG
const size_t CatalogSize = CATALOG_FILE_SIZE;
static_assert(sizeof(uuid_t) == 16, "expected type to be 16 bytes wide");
static_assert(sizeof(uint64_t) == 8, "expected 64-bit integer to be 8 bytes wide");
const size_t MaxPersistentObjects = (CATALOG_HEADER_SIZE - 32) / sizeof(CatalogEntry);
typedef struct Catalog {
    uint64_t magic;
    uint64_t object_count;
    uint64_t free_offset; // first free byte after the header (Catalog)
    uint64_t flags;
    CatalogEntry objects[MaxPersistentObjects];
} Catalog;

const uint64_t CatalogFlagCleanShutdown = 0x0000000000000001;

class NVCatalog {
    public:
        NVCatalog(string, list< pair<std::string, CatalogEntry *> >&);
        ~NVCatalog();

        CatalogEntry *add(uuid_t, uint64_t);
        void addConstructorArgs(CatalogEntry *, void *, size_t);
        void setFlags(uint64_t);
        uint64_t getFlags() { return catalog->flags; }

    private:
        Catalog *catalog = NULL;

        friend class NVManager;
};

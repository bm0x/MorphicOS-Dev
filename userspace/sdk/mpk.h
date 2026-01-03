#pragma once
#include <stdint.h>
#include <stddef.h>

// Helpers for MPK apps.
// The kernel passes a pointer to the *assets blob* as main(void* assets_ptr).
// This blob now starts with an Asset Table (Index).

struct MPKAssetTable {
    uint8_t magic[4]; // "ASST"
    uint32_t count;
};

struct MPKAssetEntry {
    char name[64];
    uint32_t offset;
    uint32_t size;
};

// Get pointer to asset data by known offset (fastest, compile-time)
static inline const uint8_t* mpk_asset_ptr(const void* assets_base, uint32_t offset)
{
    if (!assets_base) return NULL;
    return ((const uint8_t*)assets_base) + (size_t)offset;
}

// Find asset by name (runtime lookup)
// Returns pointer to data, and fills out_size if not NULL.
// Returns NULL if not found.
static inline const uint8_t* mpk_find_asset(const void* assets_base, const char* name, uint32_t* out_size)
{
    if (!assets_base) return NULL;
    
    const MPKAssetTable* table = (const MPKAssetTable*)assets_base;
    if (table->magic[0] != 'A' || table->magic[1] != 'S' || 
        table->magic[2] != 'S' || table->magic[3] != 'T') {
        return NULL; // Invalid table
    }

    const MPKAssetEntry* entries = (const MPKAssetEntry*)((const uint8_t*)assets_base + sizeof(MPKAssetTable));
    
    for (uint32_t i = 0; i < table->count; i++) {
        const char* n = entries[i].name;
        const char* q = name;
        // Simple strcmp
        while (*n && *q && *n == *q) { n++; q++; }
        if (*n == 0 && *q == 0) {
            // Match
            if (out_size) *out_size = entries[i].size;
            return ((const uint8_t*)assets_base) + entries[i].offset;
        }
    }
    
    return NULL;
}


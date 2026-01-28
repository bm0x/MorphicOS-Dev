#pragma once
#include <stdint.h>
#include <stddef.h>

struct MPKHeader {
    uint8_t magic[4];
    uint32_t manifest_off;
    uint32_t code_off;
    uint32_t code_size;
    uint32_t assets_off;
    uint32_t assets_size;
};

struct LoadedProcess {
    uint64_t entry_point;
    uint64_t stack_top;
    int error_code;
    uint64_t arg1; // First argument to user entry (e.g., assets pointer)
};

class PackageLoader {
public:
    // Load an MPK package from filesystem into memory
    // Returns LoadedProcess info. Does NOT execute it.
    static LoadedProcess Load(const char* path, uint64_t base_addr = 0x600000000000);
    
private:
    static bool VerifyMagic(const MPKHeader* header);
};

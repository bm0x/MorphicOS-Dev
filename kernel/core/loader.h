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

class PackageLoader {
public:
    // Load and execute an MPK package from filesystem
    // Returns 0 on success, error code otherwise
    static int Load(const char* path);
    
private:
    static bool VerifyMagic(const MPKHeader* header);
};

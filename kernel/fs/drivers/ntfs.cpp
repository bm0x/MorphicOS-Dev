#include "ntfs.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "../../hal/video/early_term.h"

namespace NTFS {

    IFileSystem* Mount(IBlockDevice* device, uint64_t start_lba) {
        // Read Boot Sector first to verify
        BootSector bs;
        if (!device->read_blocks(device, start_lba, 1, &bs)) {
            return nullptr;
        }
        
        // Check OEM ID "NTFS    "
        if (kstrncmp(bs.oem_id, "NTFS", 4) != 0) {
            return nullptr;
        }
        
        // Allocate Sate
        NTFSState* state = (NTFSState*)kmalloc(sizeof(NTFSState));
        state->device = device;
        state->partition_start = start_lba;
        state->bootSector = bs;
        
        // Allocate IFileSystem
        IFileSystem* fs = (IFileSystem*)kmalloc(sizeof(IFileSystem));
        kmemset(fs, 0, sizeof(IFileSystem));
        
        fs->name = "ntfs";
        fs->private_data = state;
        
        // Bind functions
        fs->read = Read;
        fs->write = Write;
        fs->readdir = Readdir;
        // Bind other stubs as NULL or stub functions
        
        EarlyTerm::Print("[NTFS] Mounted. Serial: ");
        EarlyTerm::PrintHex(bs.serial_number);
        EarlyTerm::Print("\n");
        
        return fs;
    }

    int Read(IFileSystem* self, const char* path, void* buf, size_t size, size_t offset) {
        // Stub
        return 0;
    }

    int Write(IFileSystem* self, const char* path, const void* buf, size_t size, size_t offset) {
        return -1; // Read only
    }

    int Readdir(IFileSystem* self, const char* path, void* entries, int max_entries) {
        // Stub
        return 0;
    }

}

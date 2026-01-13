#include "efi_defs.h"
#include "elf.h"
#include "../../shared/boot_info.h"

// --- Global Variables ---
EFI_SYSTEM_TABLE*  gST = nullptr;
EFI_BOOT_SERVICES* gBS = nullptr;

// --- Helper Functions ---
UINTN StrLen(const CHAR16* Str) {
    UINTN len = 0;
    while(Str[len]) len++;
    return len;
}

// Base Print for UEFI strings
void Print(CHAR16* Str) {
    gST->ConOut->OutputString(gST->ConOut, Str);
}

// Overload for C++ u"" literals
void Print(const char16_t* Str) {
    Print((CHAR16*)Str);
}

const char16_t* digits = u"0123456789ABCDEF";

void PrintHex(UINT64 Value) {
    CHAR16 buffer[20];
    buffer[18] = 0;
    int i = 17;
    do {
        buffer[i--] = digits[Value & 0xF];
        Value >>= 4;
    } while (Value > 0 || i > 15);
    Print(u"0x");
    Print(&buffer[i+1]);
    Print(u" ");
}

void PrintDec(UINT64 Value) {
    CHAR16 buffer[24];
    buffer[23] = 0;
    int i = 22;
    if (Value == 0) {
        buffer[i--] = u'0';
    } else {
        while (Value > 0) {
            buffer[i--] = u'0' + (Value % 10);
            Value /= 10;
        }
    }
    Print(&buffer[i+1]);
}

// Print 8-bit ASCII string (converted to CHAR16)
void PrintStr(const char* str) {
    while (*str) {
        CHAR16 c[2] = { (CHAR16)*str, 0 };
        Print(c);
        str++;
    }
}

// === BOOT PANIC ===
// Halts the system with detailed error info
void _BootPanic(const char* file, int line, UINT64 errCode, const char16_t* msg) {
    gST->ConOut->SetAttribute(gST->ConOut, 0x4F); // Red background, white text
    Print(u"\r\n!!! BOOT PANIC !!!\r\n");
    
    Print(u"File: ");
    PrintStr(file);
    Print(u"\r\nLine: ");
    PrintDec(line);
    Print(u"\r\nError Code: ");
    PrintHex(errCode);
    Print(u"\r\nMessage: ");
    Print(msg);
    Print(u"\r\n\r\nSystem Halted.\r\n");
    
    while(1) { __asm__ volatile("hlt"); }
}

#define BOOT_PANIC(code, msg) _BootPanic(__FILE__, __LINE__, code, msg)
#define BOOT_CHECK(status, msg) if ((status) != EFI_SUCCESS) { BOOT_PANIC((UINT64)(status), msg); }

int MemCmp(const void* a, const void* b, UINTN n) {

    const UINT8* s1 = (const UINT8*)a;
    const UINT8* s2 = (const UINT8*)b;
    for (UINTN i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
    }
    return 0;
}

// --- Logic ---

// === MEMORY MAP DUMPER ===
void PrintMemoryMap() {
    UINTN mapSize = 0, mapKey, descSize;
    UINT32 descVersion;
    EFI_MEMORY_DESCRIPTOR* map = nullptr;
    EFI_STATUS status;

    // 1. Get Buffer Size
    gBS->GetMemoryMap(&mapSize, nullptr, &mapKey, &descSize, &descVersion);
    mapSize += 4 * descSize; // Padding
    
    status = gBS->AllocatePool(EfiLoaderData, mapSize, (void**)&map);
    if (status != EFI_SUCCESS) {
        Print(u"MemMap Alloc Failed.\r\n");
        return;
    }

    // 2. Get Actual Map
    status = gBS->GetMemoryMap(&mapSize, map, &mapKey, &descSize, &descVersion);
    if (status != EFI_SUCCESS) {
        Print(u"MemMap Get Failed.\r\n");
        gBS->FreePool(map);
        return;
    }

    Print(u"\r\n--- UEFI MEMORY MAP ---\r\n");
    Print(u"Type | Physical Start | Pages\r\n");
    
    UINT8* ptr = (UINT8*)map;
    UINTN entries = mapSize / descSize;
    
    for (UINTN i = 0; i < entries; i++) {
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)ptr;
        
        // Format: [Type] [Start] [Pages]
        // Alignment formatting is manual with spaces
        PrintDec(d->Type);
        if (d->Type < 10) Print(u"    | "); else Print(u"   | ");
        
        PrintHex(d->PhysicalStart);
        Print(u" | ");
        PrintDec(d->NumberOfPages);
        Print(u"\r\n");
        
        ptr += descSize;
    }
    Print(u"-----------------------\r\n");
    gBS->FreePool(map);
}

EFI_STATUS InitializeGOP(FramebufferInfo* fbInfo) {
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = nullptr;

    EFI_STATUS status = gBS->LocateProtocol(&gopGuid, nullptr, (void**)&gop);
    if (status != EFI_SUCCESS || !gop) {
        Print(u"Error: GOP not found!\r\n");
        return status;
    }

    fbInfo->baseAddress = gop->Mode->FrameBufferBase;
    fbInfo->width = gop->Mode->Info->HorizontalResolution;
    fbInfo->height = gop->Mode->Info->VerticalResolution;
    fbInfo->pixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;
    
    // Simplified Format Detection
    switch(gop->Mode->Info->PixelFormat) {
        case PixelRedGreenBlueReserved8BitPerColor:
        case PixelBlueGreenRedReserved8BitPerColor:
        default:
            fbInfo->bytesPerPixel = 4;
    }

    Print(u"GOP Initialized. Buffer at: ");
    PrintHex(fbInfo->baseAddress);
    Print(u"\r\n");
    return EFI_SUCCESS;
}

EFI_STATUS LoadFile(CHAR16* FileName, VOID** Buffer, UINTN* Size) {
    EFI_GUID sfsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* sfs = nullptr;
    EFI_STATUS status = gBS->LocateProtocol(&sfsGuid, nullptr, (void**)&sfs);
    if (status != EFI_SUCCESS) return status;

    EFI_FILE_PROTOCOL* root = nullptr;
    status = sfs->OpenVolume(sfs, &root);
    if (status != EFI_SUCCESS) return status;

    EFI_FILE_PROTOCOL* file = nullptr;
    status = root->Open(root, &file, FileName, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS) return status;

    // Get Size
    // We allocate a small buffer for Info first
    // EFI_FILE_INFO_ID is a GUID value (initializer list), can't use sizeof on it.
    // We want the size of the ID (GUID) + some buffer for the name.
    // EFI_FILE_INFO struct: Size(64), FileSize(64), PhysicalSize(64), CreateTime, LastAccess, Modification, Attribute(64), FileName...
    // We just allocation a large enough buffer.
    UINTN infoSize = 1024;
    UINT8 infoBuffer[1024]; 
    EFI_GUID infoGuid = EFI_FILE_INFO_ID;
    
    // Actually we need the struct definition for FileInfo to get size properly.
    // For Hito 2 simplified, we can use Seek to End to get size.
    
    status = file->SetPosition(file, 0xFFFFFFFFFFFFFFFF); // Seek End
    if (status != EFI_SUCCESS) return status;
    
    UINT64 fileSize = 0;
    status = file->GetPosition(file, &fileSize);
    if (status != EFI_SUCCESS) return status;

    status = file->SetPosition(file, 0); // Seek Start
    if (status != EFI_SUCCESS) return status;

    *Size = (UINTN)fileSize;
    status = gBS->AllocatePool(EfiLoaderData, *Size, Buffer);
    if (status != EFI_SUCCESS) return status;

    status = file->Read(file, Size, *Buffer);
    file->Close(file);
    root->Close(root);

    return status;
}

EFI_STATUS LoadKernel(BootInfo* bootInfo, void** kernelEntry) {
    void* fileBuffer = nullptr;
    UINTN fileSize = 0;
    
    Print(u"Loading morph_kernel.elf...\r\n");
    EFI_STATUS status = LoadFile((CHAR16*)u"morph_kernel.elf", &fileBuffer, &fileSize);
    if (status != EFI_SUCCESS) {
        Print(u"Failed to load kernel file.\r\n");
        return status;
    }

    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)fileBuffer;
    
    // Verify Magic
    if (ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        Print(u"Invalid ELF Magic.\r\n");
        return EFI_LOAD_ERROR;
    }

    Print(u"ELF Loaded. Entry Point: ");
    PrintHex(ehdr->e_entry);
    Print(u"\r\n");

    *kernelEntry = (void*)ehdr->e_entry;

    // 1. Calculate Total Memory Range
    UINT64 minAddr = 0xFFFFFFFFFFFFFFFF;
    UINT64 maxAddr = 0;
    
    // First Pass: Determine Extents
    Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT8*)fileBuffer + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            UINT64 vaddr = phdr[i].p_vaddr;
            UINT64 memsz = phdr[i].p_memsz;
            
            if (vaddr < minAddr) minAddr = vaddr;
            if (vaddr + memsz > maxAddr) maxAddr = vaddr + memsz;
        }
    }
    
    // Align to Page Boundaries
    minAddr &= ~0xFFF;
    maxAddr = (maxAddr + 0xFFF) & ~0xFFF;
    
    UINTN totalPages = (maxAddr - minAddr) / 0x1000;
    
    Print(u"Kernel Region: [");
    PrintHex(minAddr);
    Print(u" - ");
    PrintHex(maxAddr);
    Print(u"] Pages: ");
    PrintDec(totalPages);
    Print(u"\r\n");

    // 2. Allocate Contiguous Block
    EFI_PHYSICAL_ADDRESS allocAddr = minAddr;
    status = gBS->AllocatePages(AllocateAddress, EfiLoaderCode, totalPages, &allocAddr);
    
    if (status != EFI_SUCCESS) {
        Print(u"Failed to allocate contiguous kernel memory block.\r\n");
        return status;
    }
    
    // 3. Zero Out Memory (BSS handling implicitly covered if we zero everything first)
    // Manually clearing memory (simple memset)
    UINT8* kernelMem = (UINT8*)allocAddr;
    for (UINTN i = 0; i < totalPages * 0x1000; i++) {
        kernelMem[i] = 0;
    }
    
    // 4. Load Segments
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            UINT64 fileOff = phdr[i].p_offset;
            UINT64 vaddr = phdr[i].p_vaddr;
            UINT64 filesz = phdr[i].p_filesz;
            
            // Calculate destination in our allocated block
            // Note: vaddr is physical address here (Identity mapped static kernel)
            // We can write directly to vaddr because Identity Map is active in UEFI
            
            UINT8* dest = (UINT8*)vaddr;
            UINT8* src = (UINT8*)fileBuffer + fileOff;
            
            // Debug Info
            // Print(u"Seg: "); PrintHex(vaddr); Print(u" Sz: "); PrintHex(filesz); Print(u"\r\n");
            
            for (UINTN j = 0; j < filesz; j++) {
                dest[j] = src[j];
            }
        }
    }

    // Free the file buffer? Usually yes, but for now we leave it or free it.
    gBS->FreePool(fileBuffer);
    
    return EFI_SUCCESS;
}

EFI_STATUS LoadInitRD(BootInfo* bootInfo) {
    void* fileBuffer = nullptr;
    UINTN fileSize = 0;
    
    Print(u"Loading initrd.img...\r\n");
    EFI_STATUS status = LoadFile((CHAR16*)u"initrd.img", &fileBuffer, &fileSize);
    if (status != EFI_SUCCESS) {
        Print(u"InitRD not found. System will boot without userspace apps.\r\n");
        // Not a critical error for kernel boot, but critical for apps
        bootInfo->initrdAddr = 0;
        bootInfo->initrdSize = 0;
        return EFI_SUCCESS;
    }

    // Allocate Pages for InitRD
    UINTN pages = (fileSize + 0xFFF) / 0x1000;
    EFI_PHYSICAL_ADDRESS physAddr = 0xFFFFFFFF; // Max 4GB for now
    
    // Attempt allocate anywhere
    status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &physAddr);
    if (status != EFI_SUCCESS) {
         Print(u"Failed to allocate memory for InitRD.\r\n");
         gBS->FreePool(fileBuffer);
         return status;
    }
    
    // Copy
    UINT8* dest = (UINT8*)physAddr;
    UINT8* src = (UINT8*)fileBuffer;
    for (UINTN i=0; i < fileSize; i++) dest[i] = src[i];
    
    gBS->FreePool(fileBuffer);
    
    bootInfo->initrdAddr = physAddr;
    bootInfo->initrdSize = fileSize;
    
    Print(u"InitRD Loaded at: ");
    PrintHex(physAddr);
    Print(u" Size: ");
    PrintDec(fileSize);
    Print(u"\r\n");
    
    return EFI_SUCCESS;
}

// --- Entry Point ---

extern "C" EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    gST = SystemTable;
    gBS = SystemTable->BootServices;

    gST->ConOut->Reset(gST->ConOut, false);
    gST->ConOut->SetAttribute(gST->ConOut, 0x0F); 
    gST->ConOut->ClearScreen(gST->ConOut);
    
    Print(u"--- Morphic OS: Init Booting Services ---\r\n");

    // 1. Alloc BootInfo
    BootInfo* bootInfo = nullptr;
    gBS->AllocatePool(EfiLoaderData, sizeof(BootInfo), (void**)&bootInfo);
    bootInfo->magic = 0xDEADBEEFCAFEBABE;
    
    // 2. Setup Graphics
    EFI_STATUS gopStatus = InitializeGOP(&bootInfo->framebuffer);
    BOOT_CHECK(gopStatus, u"Graphics (GOP) initialization failed.");


    // 3. Load Kernel
    PrintMemoryMap(); // Debug: Show available memory before allocation
    void (*kernelEntry)(BootInfo*) = nullptr;
    EFI_STATUS kernelStatus = LoadKernel(bootInfo, (void**)&kernelEntry);
    BOOT_CHECK(kernelStatus, u"Failed to load kernel ELF file.");

    // 3.5 Load InitRD
    LoadInitRD(bootInfo);


    Print(u"Kernel Loaded. Preparing Handover...\r\n");

    // 4. Memory Map & Handover
    // We get the map one last time right before exit
    UINTN mapKey;
    UINTN mapSize = 0;
    UINTN descriptorSize = 0;
    UINT32 descriptorVersion = 0;
    EFI_MEMORY_DESCRIPTOR* map = nullptr;

    // First call to get size
    gBS->GetMemoryMap(&mapSize, nullptr, &mapKey, &descriptorSize, &descriptorVersion);
    mapSize += 2 * descriptorSize; // Safety buffer
    gBS->AllocatePool(EfiLoaderData, mapSize, (void**)&map);
    
    // Function to retry ExitBootServices
    EFI_STATUS status = gBS->GetMemoryMap(&mapSize, map, &mapKey, &descriptorSize, &descriptorVersion);
    bootInfo->memoryMap = (MemoryDescriptor*)map;
    bootInfo->memoryMapSize = mapSize;
    bootInfo->memoryMapDescriptorSize = descriptorSize;
    
    Print(u"Exiting Boot Services...\r\n");
    
    status = gBS->ExitBootServices(ImageHandle, mapKey);
    if (status != EFI_SUCCESS) {
        // Retry logic: MapKey invalid if allocation happened (Print can alloc).
        // Get Map again immediately
        gBS->GetMemoryMap(&mapSize, map, &mapKey, &descriptorSize, &descriptorVersion);
        status = gBS->ExitBootServices(ImageHandle, mapKey);
        BOOT_CHECK(status, u"ExitBootServices failed after retry.");
    }


    // --- WE ARE NOW IN UNSAFE TERRITORY ---
    // No UEFI functions available.
    // Jump to Kernel using SysV ABI (RDI = First Argument)
    // UEFI (MS ABI) would use RCX, but our Kernel is compiled as ELF (SysV).
    
    UINT64 entryPoint = (UINT64)kernelEntry;
    __asm__ volatile (
        "cli\n\t"            // Disable Interrupts
        "mov %0, %%rdi\n\t"  // Arg1 = bootInfo
        "mov %1, %%rax\n\t"  // Entry
        "jmp *%%rax\n\t"     // Jump (No return)
        : 
        : "r"(bootInfo), "r"(entryPoint)
        : "rdi", "rax", "memory"
    );

    while(1) {}

    return EFI_SUCCESS;
}

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

    // Load Segments
    Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT8*)fileBuffer + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            UINTN pages = (phdr[i].p_memsz + 0xFFF) / 0x1000;
            UINT64 targetAddr = phdr[i].p_vaddr;
            
            // Allocate Memory at precise address
            // We MUST use AllocateAddress (2) because the ELF is not Position Independent.
            // It expects to run at 0x100000.
            
            status = gBS->AllocatePages(2 /* AllocateAddress */, 
                                        EfiLoaderCode, pages, &targetAddr);
            
            if (status != EFI_SUCCESS) {
                 Print(u"Error: Could not allocate kernel memory at specific address: ");
                 PrintHex(targetAddr);
                 Print(u"\r\n");
                 return status;
            }
            // For Hito 2, we assume 0x100000 range is free (usually is).
            
            // Copy segment
            UINT8* dest = (UINT8*)targetAddr; // Physical mapping assumption
            UINT8* src = (UINT8*)fileBuffer + phdr[i].p_offset;
            for (UINTN j = 0; j < phdr[i].p_filesz; j++) {
                dest[j] = src[j];
            }
            // Zero out BSS
            for (UINTN j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) {
                dest[j] = 0;
            }
        }
    }

    return EFI_SUCCESS;
}

// --- Entry Point ---

extern "C" EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    gST = SystemTable;
    gBS = SystemTable->BootServices;

    gST->ConOut->Reset(gST->ConOut, false);
    gST->ConOut->SetAttribute(gST->ConOut, 0x0F); 
    gST->ConOut->ClearScreen(gST->ConOut);
    
    Print(u"--- Morphic OS: Phase Origin Hito 2 ---\r\n");

    // 1. Alloc BootInfo
    BootInfo* bootInfo = nullptr;
    gBS->AllocatePool(EfiLoaderData, sizeof(BootInfo), (void**)&bootInfo);
    bootInfo->magic = 0xDEADBEEFCAFEBABE;
    
    // 2. Setup Graphics
    EFI_STATUS gopStatus = InitializeGOP(&bootInfo->framebuffer);
    BOOT_CHECK(gopStatus, u"Graphics (GOP) initialization failed.");


    // 3. Load Kernel
    void (*kernelEntry)(BootInfo*) = nullptr;
    EFI_STATUS kernelStatus = LoadKernel(bootInfo, (void**)&kernelEntry);
    BOOT_CHECK(kernelStatus, u"Failed to load kernel ELF file.");


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

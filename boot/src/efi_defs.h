#ifndef EFI_DEFS_H
#define EFI_DEFS_H

#include <stdint.h> // For uintN_t types

// UEFI uses the Microsoft ABI for x86_64
#define EFIAPI __attribute__((ms_abi))

// Basic Types
typedef uint8_t     UINT8;
typedef uint16_t    UINT16;
typedef uint32_t    UINT32;
typedef uint64_t    UINT64;
typedef uint64_t    UINTN;
typedef int64_t     INTN; // Keep INTN as it was
typedef uint8_t     BOOLEAN; // Keep BOOLEAN as it was
typedef uint16_t    CHAR16;
typedef void        VOID;
typedef void*       EFI_HANDLE;
typedef uint64_t    EFI_STATUS;
typedef uint64_t    EFI_PHYSICAL_ADDRESS;
typedef uint64_t    EFI_VIRTUAL_ADDRESS;

#include "../../shared/boot_info.h" // For definitions like physical_addr_t if needed, or define here.

// Status Codes
#define EFI_SUCCESS               0
#define EFI_LOAD_ERROR            (0x8000000000000001)
#define EFI_INVALID_PARAMETER     (0x8000000000000002)
#define EFI_UNSUPPORTED           (0x8000000000000003)
#define EFI_BUFFER_TOO_SMALL      (0x8000000000000005)
#define EFI_NOT_FOUND             (0x800000000000000E)

// Global IDs (GUIDs)
typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

// Handles
typedef VOID* EFI_HANDLE;

// Protocols Forward Declarations
struct EFI_SYSTEM_TABLE;
struct EFI_BOOT_SERVICES;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL;

// --- Protocol Definitions ---

// Simple Text Output (Console)
typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_TEST_STRING)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber, UINTN *Columns, UINTN *Rows);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET          Reset;
    EFI_TEXT_STRING         OutputString;
    EFI_TEXT_TEST_STRING    TestString;
    EFI_TEXT_QUERY_MODE     QueryMode;
    EFI_TEXT_SET_MODE       SetMode;
    EFI_TEXT_SET_ATTRIBUTE  SetAttribute;
    EFI_TEXT_CLEAR_SCREEN   ClearScreen;
    // ... complete struct usually larger, but we only need these for now.
    // WARNING: In strict C++, incomplete structs can be dangerous if not carefully handled.
    // For full compliance we should define the rest or stick to what we use.
    // Adding dummy pointers for safety if needed.
};

// Graphics Output Protocol (GOP)
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a} }

typedef struct {
    UINT32            RedMask;
    UINT32            GreenMask;
    UINT32            BlueMask;
    UINT32            ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32                     Version;
    UINT32                     HorizontalResolution;
    UINT32                     VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT  PixelFormat;
    EFI_PIXEL_BITMASK          PixelInformation;
    UINT32                     PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                                 MaxMode;
    UINT32                                 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION   *Info;
    UINTN                                  SizeOfInfo;
    physical_addr_t                        FrameBufferBase;
    UINTN                                  FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void* QueryMode; // Placeholder
    void* SetMode;   // Placeholder
    void* Blt;       // Placeholder
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;


// Boot Services
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32          Type;
    UINT64          PhysicalStart;
    UINT64          VirtualStart;
    UINT64          NumberOfPages;
    UINT64          Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(int Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages, UINT64 *Memory);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(UINT64 Memory, UINTN Pages);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, UINTN *MapKey, UINTN *DescriptorSize, UINT32 *DescriptorVersion);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE PoolType, UINTN Size, VOID **Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE ImageHandle, UINTN MapKey);
// LocateProtocol
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *Protocol, VOID *Registration, VOID **Interface);


typedef struct EFI_BOOT_SERVICES {
    char                _pad1[24]; // Skip header
    
    // Task Priority Services
    void*               RaiseTPL;
    void*               RestoreTPL;

    // Memory Services
    EFI_ALLOCATE_PAGES  AllocatePages; // 0x18 offset? No, header is 24 bytes (Signature 8, Revision 4, HeaderSize 4, CRC32 4, Reserved 4) 
                                       // Actually Header is table generic header.
                                       // Let's rely on standard offset calculation or just padding.
                                       // Table Header: UINT64 Signature, UINT32 Revision, UINT32 HeaderSize, UINT32 CRC32, UINT32 Reserved. Total 24 bytes.
    EFI_FREE_PAGES      FreePages;
    EFI_GET_MEMORY_MAP  GetMemoryMap;
    EFI_ALLOCATE_POOL   AllocatePool;
    EFI_FREE_POOL       FreePool;

    // Event & Timer Services
    void*               CreateEvent;
    void*               SetTimer;
    void*               WaitForEvent;
    void*               SignalEvent;
    void*               CloseEvent;
    void*               CheckEvent;

    // Protocol Handler Services
    void*               InstallProtocolInterface;
    void*               ReinstallProtocolInterface;
    void*               UninstallProtocolInterface;
    void*               HandleProtocol;
    void*               Reserved;
    void*               RegisterProtocolNotify;
    void*               LocateHandle;
    void*               LocateDevicePath;
    void*               InstallConfigurationTable;

    // Image Services
    void*               LoadImage;
    void*               StartImage;
    void*               Exit;
    void*               UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;

    // Misc Services
    void*               GetNextMonotonicCount;
    void*               Stall;
    void*               SetWatchdogTimer;

    // DriverSupport Services
    void*               ConnectController;
    void*               DisconnectController;

    // Open and Close Protocol Services
    void*               OpenProtocol;
    void*               CloseProtocol;
    void*               OpenProtocolInformation;

    // Library Services
    void*               ProtocolsPerHandle;
    void*               LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    // ...
} EFI_BOOT_SERVICES;


// --- File System Protocols ---

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }

#define EFI_FILE_INFO_ID \
    { 0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }

typedef struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(struct EFI_FILE_PROTOCOL *This, struct EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
    EFI_STATUS (EFIAPI *Close)(struct EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Delete)(struct EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Read)(struct EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
    EFI_STATUS (EFIAPI *Write)(struct EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
    EFI_STATUS (EFIAPI *GetPosition)(struct EFI_FILE_PROTOCOL *This, UINT64 *Position);
    EFI_STATUS (EFIAPI *SetPosition)(struct EFI_FILE_PROTOCOL *This, UINT64 Position);
    EFI_STATUS (EFIAPI *GetInfo)(struct EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN *BufferSize, VOID *Buffer);
    EFI_STATUS (EFIAPI *SetInfo)(struct EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN *BufferSize, VOID *Buffer);
    EFI_STATUS (EFIAPI *Flush)(struct EFI_FILE_PROTOCOL *This);
} EFI_FILE_PROTOCOL;

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

#define EFI_FILE_MODE_READ   0x0000000000000001
#define EFI_FILE_READ_ONLY   0x0000000000000001



// System Table
typedef struct EFI_SYSTEM_TABLE {
    char                             _header[24];
    CHAR16                           *FirmwareVendor;
    UINT32                           FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    void*                            ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *StdErr;
    void*                            RuntimeServices;
    EFI_BOOT_SERVICES                *BootServices;
    UINTN                            NumberOfTableEntries;
    void*                            ConfigurationTable;
} EFI_SYSTEM_TABLE;

#endif // EFI_DEFS_H

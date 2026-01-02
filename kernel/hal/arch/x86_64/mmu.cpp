// x86_64 MMU Implementation
// Page table management for x86-64 long mode (4-level paging)

#include "../../../../kernel/arch/common/mmu.h"
#include "../../../../kernel/mm/heap.h"
#include "../../../../kernel/mm/pmm.h"
#include "../../../../kernel/utils/std.h"
#include "../../serial/uart.h"


// x86-64 Page Table Entry bits
#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_PWT      (1ULL << 3)  // Write-through
#define PTE_PCD      (1ULL << 4)  // Cache disable
#define PTE_ACCESSED (1ULL << 5)
#define PTE_DIRTY    (1ULL << 6)
#define PTE_HUGE     (1ULL << 7)  // 2MB/1GB page
#define PTE_GLOBAL   (1ULL << 8)
#define PTE_NX       (1ULL << 63) // No-execute

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

// Page table levels
#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

// CR0 bit 16 = WP (Write Protect)
// When set, the CPU enforces read-only protection even in kernel mode.
// UEFI page tables are often marked read-only, so we must disable WP to modify them.
static inline void DisableWriteProtect() {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 16);  // Clear WP bit
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
}

static inline void EnableWriteProtect() {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16);   // Set WP bit
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
}

namespace MMU {
    static uint64_t* kernel_pml4 = nullptr;
    
    // Convert generic flags to x86 PTE flags
    static uint64_t ConvertFlags(uint32_t flags) {
        uint64_t pte_flags = PTE_PRESENT;
        
        if (flags & PAGE_WRITABLE) pte_flags |= PTE_WRITABLE;
        if (flags & PAGE_USER) pte_flags |= PTE_USER;
        if (flags & PAGE_NOCACHE) pte_flags |= PTE_PCD;
        if (flags & PAGE_WRITETHROUGH) pte_flags |= PTE_PWT;
        if (flags & PAGE_GLOBAL) pte_flags |= PTE_GLOBAL;
        if (!(flags & PAGE_EXECUTABLE)) pte_flags |= PTE_NX;
        
        return pte_flags;
    }
    
    // Allocate a page-aligned page for page tables
    // CRITICAL: Page tables MUST be 4KB-aligned!
    // kmalloc only aligns to 16 bytes, so we over-allocate and round up.
    static uint64_t* AllocateTable() {
        // Allocate 8KB to guarantee we can find a 4KB-aligned region within
        void* raw_ptr = kmalloc(4096 + 4096);
        if (!raw_ptr) {
            UART::Write("[MMU] ERROR: AllocateTable failed - kmalloc returned null\n");
            return nullptr;
        }
        
        // Round up to next 4KB boundary
        uint64_t aligned_addr = ((uint64_t)raw_ptr + 0xFFF) & ~0xFFFULL;
        uint64_t* table = (uint64_t*)aligned_addr;
        
        // Clear the page using a direct loop (kmemset might fail with volatile memory)
        for (int i = 0; i < 512; i++) {
            table[i] = 0;
        }
        
        // DEBUG: Verify table is zeroed
        if (table[0] != 0 || table[1] != 0) {
            UART::Write("[MMU] ERROR: Table NOT zeroed! table[0]=");
            UART::WriteHex(table[0]);
            UART::Write("\n");
        }
        
        return table;
    }
    
    void Init() {
        // Read current CR3 to get existing kernel page table
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        kernel_pml4 = (uint64_t*)(cr3 & PTE_ADDR_MASK);
    }
    
    bool MapPage(uint64_t virt, uint64_t phys, uint32_t flags) {
        // DEBUG: Trace MapPage calls for user-space addresses
        if (virt == 0x8000000000ULL) {
            UART::Write("\n[MMU] MapPage called for 0x8000000000\n");
            UART::Write("  phys: "); UART::WriteHex(phys); UART::Write("\n");
            UART::Write("  flags: "); UART::WriteHex(flags); UART::Write("\n");
        }
        
        // CRITICAL: Disable Write Protect to allow modifying UEFI page tables
        // UEFI marks its page tables as read-only, causing Page Fault if we try to write
        DisableWriteProtect();
        
        uint64_t pte_flags = ConvertFlags(flags);
        
        // Flags to verify/add to parent directories if they exist
        uint64_t dir_flags = PTE_PRESENT;
        if (flags & PAGE_WRITABLE) dir_flags |= PTE_WRITABLE;
        if (flags & PAGE_USER) dir_flags |= PTE_USER;
        
        // CRITICAL: For executable pages, we must CLEAR NX bit from directory entries
        // In x86-64, NX is restrictive: if ANY level has NX=1, execution is blocked
        bool needs_executable = (flags & PAGE_EXECUTABLE) != 0;
        
        // Get or create page table entries at each level
        uint64_t* pml4 = kernel_pml4;
        if (!pml4) {
            EnableWriteProtect();
            return false;
        }
        
        // PML4 -> PDPT
        uint64_t* pdpt;
        
        // Debug: Check what UEFI has in PML4
        if (virt == 0x8000000000ULL) {
            UART::Write("  PML4 addr: "); UART::WriteHex((uint64_t)pml4); UART::Write("\n");
            UART::Write("  PML4_INDEX(virt): "); UART::WriteDec(PML4_INDEX(virt)); UART::Write("\n");
            UART::Write("  PML4[index] = "); UART::WriteHex(pml4[PML4_INDEX(virt)]); UART::Write("\n");
        }
        
        if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) {
            pdpt = AllocateTable();
            if (!pdpt) {
                if (virt == 0x8000000000ULL) UART::Write("  FAIL: AllocTable for PDPT\n");
                return false;
            }
            pml4[PML4_INDEX(virt)] = ((uint64_t)pdpt & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
            if (virt == 0x8000000000ULL) {
                UART::Write("  Created new PDPT at: "); UART::WriteHex((uint64_t)pdpt); UART::Write("\n");
            }
        } else {
            // FIX: Ensure existing directory entry propagates permissions
            pml4[PML4_INDEX(virt)] |= dir_flags;
            // CRITICAL: Clear NX bit from directory if mapping executable page
            if (needs_executable) {
                pml4[PML4_INDEX(virt)] &= ~PTE_NX;
            }
            pdpt = (uint64_t*)(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
        }
        
        // PDPT -> PD
        uint64_t* pd;
        if (!(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) {
            pd = AllocateTable();
            if (!pd) {
                if (virt == 0x8000000000ULL) UART::Write("  FAIL: AllocTable for PD\n");
                return false;
            }
            pdpt[PDPT_INDEX(virt)] = ((uint64_t)pd & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
            if (virt == 0x8000000000ULL) {
                UART::Write("  Created new PD at: "); UART::WriteHex((uint64_t)pd); UART::Write("\n");
            }
        } else {
            // Check for Huge Page collision
            if (pdpt[PDPT_INDEX(virt)] & PTE_HUGE) {
                 if (virt == 0x8000000000ULL) UART::Write("  FAIL: PDPT huge page collision\n");
                 return false;
            }
            // FIX: Ensure existing directory entry propagates permissions
            pdpt[PDPT_INDEX(virt)] |= dir_flags;
            // CRITICAL: Clear NX bit from directory if mapping executable page
            if (needs_executable) {
                pdpt[PDPT_INDEX(virt)] &= ~PTE_NX;
            }
            pd = (uint64_t*)(pdpt[PDPT_INDEX(virt)] & PTE_ADDR_MASK);
        }
        
        // PD -> PT
        uint64_t* pt;
        if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) {
            pt = AllocateTable();
            if (!pt) {
                if (virt == 0x8000000000ULL) UART::Write("  FAIL: AllocTable for PT\n");
                return false;
            }
            pd[PD_INDEX(virt)] = ((uint64_t)pt & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
            if (virt == 0x8000000000ULL) {
                UART::Write("  Created new PT at: "); UART::WriteHex((uint64_t)pt); UART::Write("\n");
            }
        } else {
            // Check for Huge Page collision
            if (pd[PD_INDEX(virt)] & PTE_HUGE) {
                 if (virt == 0x8000000000ULL) {
                     UART::Write("  FAIL: PD huge page collision\n");
                     UART::Write("  PD addr: "); UART::WriteHex((uint64_t)pd); UART::Write("\n");
                     UART::Write("  PD_INDEX: "); UART::WriteDec(PD_INDEX(virt)); UART::Write("\n");
                     UART::Write("  PD entry value: "); UART::WriteHex(pd[PD_INDEX(virt)]); UART::Write("\n");
                 }
                 EnableWriteProtect();
                 return false;
            }
            // FIX: Ensure existing directory entry propagates permissions
            pd[PD_INDEX(virt)] |= dir_flags;
            // CRITICAL: Clear NX bit from directory if mapping executable page
            if (needs_executable) {
                pd[PD_INDEX(virt)] &= ~PTE_NX;
            }
            pt = (uint64_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
        }
        
        // PT -> Page
        uint64_t pte_value = (phys & PTE_ADDR_MASK) | pte_flags;
        pt[PT_INDEX(virt)] = pte_value;
        
        // DEBUG: Detailed output for FIRST user page
        if (virt >= 0x8000000000ULL && virt < 0x8000001000ULL) {
            UART::Write("\n[MMU-DEBUG] Mapping first user page:\n");
            UART::Write("  virt: "); UART::WriteHex(virt); UART::Write("\n");
            UART::Write("  phys: "); UART::WriteHex(phys); UART::Write("\n");
            UART::Write("  input flags: "); UART::WriteHex(flags); UART::Write("\n");
            UART::Write("  pte_flags: "); UART::WriteHex(pte_flags); UART::Write("\n");
            UART::Write("  pte_value: "); UART::WriteHex(pte_value); UART::Write("\n");
            UART::Write("  NX bit in PTE: "); UART::WriteDec((pte_value >> 63) & 1); UART::Write("\n");
            UART::Write("  USER bit in PTE: "); UART::WriteDec((pte_value >> 2) & 1); UART::Write("\n");
            UART::Write("  needs_executable: "); UART::WriteDec(needs_executable ? 1 : 0); UART::Write("\n");
        }
        
        // DEBUG: Hierarchy Dump for User Address
        if (virt == 0x8000000000ULL) {
            UART::Write("\n[MMU] HIERARCHY CHECK for 0x8000000000:\n");
            
            uint64_t pml4_e = pml4[PML4_INDEX(virt)];
            UART::Write("  PML4["); UART::WriteDec(PML4_INDEX(virt)); UART::Write("]: "); UART::WriteHex(pml4_e);
            UART::Write(" NX="); UART::WriteDec((pml4_e >> 63) & 1);
            UART::Write(" U="); UART::WriteDec((pml4_e >> 2) & 1);
            UART::Write(" W="); UART::WriteDec((pml4_e >> 1) & 1);
            UART::Write("\n");

            uint64_t pdpt_e = pdpt[PDPT_INDEX(virt)];
            UART::Write("  PDPT["); UART::WriteDec(PDPT_INDEX(virt)); UART::Write("]: "); UART::WriteHex(pdpt_e);
            UART::Write(" NX="); UART::WriteDec((pdpt_e >> 63) & 1);
            UART::Write(" U="); UART::WriteDec((pdpt_e >> 2) & 1);
            UART::Write("\n");

            uint64_t pd_e = pd[PD_INDEX(virt)];
            UART::Write("  PD  ["); UART::WriteDec(PD_INDEX(virt)); UART::Write("]: "); UART::WriteHex(pd_e);
            UART::Write(" NX="); UART::WriteDec((pd_e >> 63) & 1);
            UART::Write(" U="); UART::WriteDec((pd_e >> 2) & 1);
            UART::Write("\n");

            uint64_t pt_e = pt[PT_INDEX(virt)];
            UART::Write("  PT  ["); UART::WriteDec(PT_INDEX(virt)); UART::Write("]: "); UART::WriteHex(pt_e);
            UART::Write(" NX="); UART::WriteDec((pt_e >> 63) & 1);
            UART::Write(" U="); UART::WriteDec((pt_e >> 2) & 1);
            UART::Write("\n");
        }
        
        // Flush TLB for this address
        FlushTLB(virt);
        
        // Restore Write Protect
        EnableWriteProtect();
        
        return true;
    }
    
    bool MapRange(uint64_t virt_start, uint64_t phys_start, uint64_t size, uint32_t flags) {
        for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE_4K) {
            if (!MapPage(virt_start + offset, phys_start + offset, flags)) {
                return false;
            }
        }
        return true;
    }
    
    void UnmapPage(uint64_t virt) {
        if (!kernel_pml4) return;
        
        uint64_t* pdpt = (uint64_t*)(kernel_pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
        if (!pdpt || !(kernel_pml4[PML4_INDEX(virt)] & PTE_PRESENT)) return;
        
        uint64_t* pd = (uint64_t*)(pdpt[PDPT_INDEX(virt)] & PTE_ADDR_MASK);
        if (!pd || !(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) return;
        
        uint64_t* pt = (uint64_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
        if (!pt || !(pd[PD_INDEX(virt)] & PTE_PRESENT)) return;
        
        pt[PT_INDEX(virt)] = 0;
        FlushTLB(virt);
    }
    
    void FlushTLB(uint64_t virt) {
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
    
    void FlushTLBAll() {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    }
    
    uint64_t GetPhysical(uint64_t virt) {
        if (!kernel_pml4) return 0;
        
        if (!(kernel_pml4[PML4_INDEX(virt)] & PTE_PRESENT)) return 0;
        uint64_t* pdpt = (uint64_t*)(kernel_pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
        
        if (!(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) return 0;
        uint64_t* pd = (uint64_t*)(pdpt[PDPT_INDEX(virt)] & PTE_ADDR_MASK);
        
        if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) return 0;
        uint64_t* pt = (uint64_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
        
        if (!(pt[PT_INDEX(virt)] & PTE_PRESENT)) return 0;
        
        return (pt[PT_INDEX(virt)] & PTE_ADDR_MASK) | (virt & 0xFFF);
    }
    
    void SwitchPageTable(uint64_t table_phys) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(table_phys) : "memory");
    }
    
    uint64_t GetCurrentPageTable() {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        return cr3 & PTE_ADDR_MASK;
    }
    
    uint64_t CreatePageTable() {
        uint64_t* pml4 = AllocateTable();
        if (!pml4) return 0;
        
        // Copy kernel mappings (upper half)
        if (kernel_pml4) {
            for (int i = 256; i < 512; i++) {
                pml4[i] = kernel_pml4[i];
            }
        }
        
        return (uint64_t)pml4;
    }
    
    uint64_t ClonePageTable() {
        // TODO: Implement deep clone for fork
        return CreatePageTable();
    }
    
    void DestroyPageTable(uint64_t table_phys) {
        kfree((void*)table_phys);
    }
    
    bool IsMapped(uint64_t virt) {
        return GetPhysical(virt) != 0;
    }
    
    uint32_t GetPageFlags(uint64_t virt) {
        // TODO: Convert x86 flags back to generic
        return 0;
    }
}

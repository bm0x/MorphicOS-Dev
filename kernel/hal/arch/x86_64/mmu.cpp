// x86_64 MMU Implementation
// Page table management for x86-64 long mode (4-level paging)

#include "../../../../kernel/arch/common/mmu.h"
#include "../../../../kernel/mm/heap.h"
#include "../../../../kernel/utils/std.h"


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
    
    // Allocate a page-aligned page for tables
    static uint64_t* AllocateTable() {
        void* table = kmalloc(4096);
        if (!table) return nullptr;
        kmemset(table, 0, 4096);
        return (uint64_t*)table;
    }
    
    void Init() {
        // Read current CR3 to get existing kernel page table
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        kernel_pml4 = (uint64_t*)(cr3 & PTE_ADDR_MASK);
    }
    
    bool MapPage(uint64_t virt, uint64_t phys, uint32_t flags) {
        uint64_t pte_flags = ConvertFlags(flags);
        
        // Get or create page table entries at each level
        uint64_t* pml4 = kernel_pml4;
        if (!pml4) return false;
        
        // PML4 -> PDPT
        uint64_t* pdpt;
        if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) {
            pdpt = AllocateTable();
            if (!pdpt) return false;
            pml4[PML4_INDEX(virt)] = ((uint64_t)pdpt & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        } else {
            pdpt = (uint64_t*)(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
        }
        
        // PDPT -> PD
        uint64_t* pd;
        if (!(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) {
            pd = AllocateTable();
            if (!pd) return false;
            pdpt[PDPT_INDEX(virt)] = ((uint64_t)pd & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        } else {
            pd = (uint64_t*)(pdpt[PDPT_INDEX(virt)] & PTE_ADDR_MASK);
        }
        
        // PD -> PT
        uint64_t* pt;
        if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) {
            pt = AllocateTable();
            if (!pt) return false;
            pd[PD_INDEX(virt)] = ((uint64_t)pt & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        } else {
            pt = (uint64_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
        }
        
        // PT -> Page
        pt[PT_INDEX(virt)] = (phys & PTE_ADDR_MASK) | pte_flags;
        
        // Flush TLB for this address
        FlushTLB(virt);
        
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

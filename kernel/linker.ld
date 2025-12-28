/*
 * Morphic OS Kernel Linker Script
 * Target: x86_64 ELF
 */

ENTRY(kernel_main)

SECTIONS
{
    /* 
     * We want the kernel to be loaded at 2MB (0x200000) physical address.
     * This avoids BIOS/UEFI reserved areas and 1MB fragmentation.
     */
    . = 0x200000;

    .text : ALIGN(4096)
    {
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4096)
    {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4096)
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}

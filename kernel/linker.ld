/*
 * Morphic OS Kernel Linker Script
 * Target: x86_64 ELF
 */

ENTRY(_start)

SECTIONS
{
    /* 
     * We want the kernel to be loaded at 1GB (0x40000000) physical address.
     * This moves the kernel to a "Safe Zone" far above standard UEFI/Firmware reservations.
     * Requires the VM to have > 1GB RAM (recommend -m 4G).
     */
    . = 0x40000000;

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

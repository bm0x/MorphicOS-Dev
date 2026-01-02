ENTRY(main)
SECTIONS
{
    . = 0x600000000000; /* Load address for userspace apps (matches loader.cpp) */
    
    .text : {
        *(.text)
    }
    
    .rodata : {
        *(.rodata)
    }
    
    .data : {
        *(.data)
    }
    
    .bss : {
        *(.bss)
    }
}

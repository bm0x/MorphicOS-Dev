ENTRY(_start)
SECTIONS
{
    . = 0x600000000000;
    
    .text : {
        *(.text.entry)
        *(.text*)
    } :text
    
    . = ALIGN(0x1000);
    
    .rodata : {
        *(.rodata*)
    } :rodata
    
    . = ALIGN(0x1000);
    
    .data : {
        *(.data*)
    } :data
    
    . = ALIGN(0x1000);
    
    .bss : {
        *(.bss*)
        *(COMMON)
    } :bss
    
    /DISCARD/ : {
        *(.eh_frame)
        *(.note*)
    }
}

PHDRS
{
    text PT_LOAD FLAGS(5);   /* RX */
    rodata PT_LOAD FLAGS(4); /* R  */
    data PT_LOAD FLAGS(6);   /* RW */
    bss PT_LOAD FLAGS(6);    /* RW */
}

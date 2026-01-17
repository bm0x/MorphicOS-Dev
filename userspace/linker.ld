ENTRY(_start)
SECTIONS
{
    . = 0x600000000000;
    
    .text : {
        *(.text.entry)
        *(.text*)
        *(.ltext)
        *(.ltext.*)
    } :text
    
    . = ALIGN(0x1000);
    
    .rodata : {
        *(.rodata*)
        *(.lrodata)
        *(.lrodata.*)
    } :rodata
    
    . = ALIGN(0x1000);
    
    .data : {
        *(.data*)
        *(.ldata)
        *(.ldata.*)
    } :data
    
    . = ALIGN(0x1000);
    
    .bss : {
        *(.bss*)
        *(.lbss)
        *(.lbss.*)
        *(COMMON)
    } :bss
    
    /DISCARD/ : {
        *(.eh_frame)
        *(.note*)
        *(.group)
        *(.llvm_addrsig)
    }
}

PHDRS
{
    text PT_LOAD FLAGS(5);   /* RX */
    rodata PT_LOAD FLAGS(4); /* R  */
    data PT_LOAD FLAGS(6);   /* RW */
    bss PT_LOAD FLAGS(6);    /* RW */
}

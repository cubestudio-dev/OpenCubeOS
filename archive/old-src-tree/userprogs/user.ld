ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}

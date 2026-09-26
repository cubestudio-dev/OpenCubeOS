ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data : {
        *(.data) *(.data.*)
        *(.bss) *(.bss.*) *(COMMON)
    }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}

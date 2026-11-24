OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS
{
    . = 0x7e00;
    .text : { KEEP(*(.text.entry)) *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    .data : { *(.data*) } :data
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) } :data
    __bss_end = .;
    ASSERT(__bss_end < 0x97000, "Kernel overlaps protected-mode stack guard (32 KiB)")
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}

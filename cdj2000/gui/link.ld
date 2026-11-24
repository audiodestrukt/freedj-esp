/* The display processor's program: code in L1 instruction SRAM (the boot
 * ROM enters at 0xFFA08000), data in L1 data bank A (the core can't read
 * data out of instruction SRAM). */
OUTPUT_ARCH(bfin)
ENTRY(start)
MEMORY
{
    l1code (rx) : ORIGIN = 0xFFA08000, LENGTH = 32K
    l1data (rw) : ORIGIN = 0xFF804000, LENGTH = 16K
}
SECTIONS
{
    .text : { KEEP(*(.text.start)) *(.text .text.*) } > l1code
    .data : ALIGN(4) { *(.rodata .rodata.*) *(.data .data.*) } > l1data
    .bss (NOLOAD) : ALIGN(4) { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); __bss_end = .; } > l1data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

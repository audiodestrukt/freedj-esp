/* The display processor's program: code in L1 instruction SRAM (the boot
 * ROM enters at 0xFFA08000); its data (the fonts, its state) in SDRAM from
 * 0x20000, below the framebuffers at 0x100000 and 0x140000, the peaks at
 * 0x200000 and the receive buffer at 0x300000 (fixed addresses, gui.c).
 * The core can't read data out of instruction SRAM; the stack is in the L1
 * scratchpad (crt0.S). */
OUTPUT_ARCH(bfin)
ENTRY(start)
MEMORY
{
    l1code (rx) : ORIGIN = 0xFFA08000, LENGTH = 32K
    sdram  (rw) : ORIGIN = 0x00020000, LENGTH = 0xE0000
}
SECTIONS
{
    .text : { KEEP(*(.text.start)) *(.text .text.*) } > l1code
    .data : ALIGN(4) { *(.rodata .rodata.*) *(.data .data.*) } > sdram
    .bss (NOLOAD) : ALIGN(4) { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); __bss_end = .; } > sdram
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

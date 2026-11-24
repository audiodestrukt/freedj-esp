/* The main board's image, as the CDJ-2000 enters it: a flat binary at the
 * DRAM base (physical 0x04000000, 64 MB), entered at 0x800 through the
 * uncached P2 alias, 0xA4000800, with SP at the top of DRAM.  (The emulator's
 * -kernel path loads and enters it that way; on the deck the bootloader does
 * after unpacking it from flash: packaging is milestone 7.)  Running from P2
 * keeps the caches out of it until they are set up. */
OUTPUT_ARCH(sh)
ENTRY(_start)
MEMORY { dram (rwx) : ORIGIN = 0xA4000000, LENGTH = 64M }
SECTIONS
{
    .head 0xA4000000 : { KEEP(*(.head)) . = 0x800; } > dram
    .text 0xA4000800 : { KEEP(*(.text.start)) *(.text .text.*) } > dram
    .rodata : { *(.rodata .rodata.*) } > dram
    .init_array : ALIGN(4) { __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; } > dram
    /* libgcc is built position-independent: its calls between its own
     * routines go through the GOT, which must be in the image. */
    .data : ALIGN(4) { *(.data .data.*) *(.got.plt) *(.got) *(.data.rel .data.rel.*) } > dram
    .bss (NOLOAD) : ALIGN(4) { _bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; } > dram
    _end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

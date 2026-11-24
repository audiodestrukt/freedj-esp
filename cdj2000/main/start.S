/* Entry: the loader leaves SR = MD 1, BL 1, IMASK 15, bank 0, and SP at the
 * top of DRAM.  Set our own stack anyway, set the FPU's mode (the loader
 * leaves it in single precision, and GCC switches with fpchg from double),
 * zero .bss, run the static constructors, run main(). */
#include "board.h"
    .section .head, "a"
    .ascii  "freedj main image for the " BOARD_NAME "\0"

    .section .text.start, "ax"
    .global _start
_start:
    mov.l   stack_top, r15
    mov.l   fpscr_init, r0      /* the FPU as GCC's SH-4 code expects it at entry: */
    lds     r0, fpscr           /* double precision (PR), round to nearest, no traps */
    mov.l   bss_start, r1
    mov.l   bss_end, r2
    mov     #0, r0
1:  cmp/hs  r2, r1
    bt      2f
    mov.l   r0, @r1
    bra     1b
    add     #4, r1
2:  mov.l   ctors_addr, r0  /* static constructors */
    jsr     @r0
    nop
    mov.l   main_addr, r0
    jsr     @r0
    nop
3:  bra     3b          /* main returned: stay here */
    nop

    .align 2
stack_top:  .long BOARD_STACK_TOP
fpscr_init: .long 0x00080000
bss_start:  .long _bss_start
bss_end:    .long _bss_end
main_addr:  .long main
ctors_addr: .long run_constructors

    .section .note.GNU-stack, "", @progbits

/* Entry: the loader leaves SR = MD 1, BL 1, IMASK 15, bank 0, and SP at the
 * top of DRAM.  Set our own stack anyway, zero .bss, run main(). */
    .section .head, "a"
    .ascii  "freedj cdj2000 main image\0"

    .section .text.start, "ax"
    .global _start
_start:
    mov.l   stack_top, r15
    mov.l   bss_start, r1
    mov.l   bss_end, r2
    mov     #0, r0
1:  cmp/hs  r2, r1
    bt      2f
    mov.l   r0, @r1
    bra     1b
    add     #4, r1
2:  mov.l   main_addr, r0
    jsr     @r0
    nop
3:  bra     3b          /* main returned: stay here */
    nop

    .align 2
stack_top:  .long 0xA7FFFFC0
bss_start:  .long _bss_start
bss_end:    .long _bss_end
main_addr:  .long main

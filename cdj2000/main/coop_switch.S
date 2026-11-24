/* coop_switch(from, to): save the callee-saved state in *from, load *to.
 * SH-4 GCC ABI: r8-r15, PR, MACH/MACL, GBR, FPSCR and fr12-fr15 survive a call.
 * Layout (words): r8..r15, pr, mach, macl, gbr, fpscr, fr12..fr15. */
    .text
    .global coop_switch
    .align 2
coop_switch:
    add     #68, r4         /* push downwards from the end of *from */
    fmov.s  fr15, @-r4
    fmov.s  fr14, @-r4
    fmov.s  fr13, @-r4
    fmov.s  fr12, @-r4
    sts.l   fpscr, @-r4
    stc.l   gbr, @-r4
    sts.l   macl, @-r4
    sts.l   mach, @-r4
    sts.l   pr, @-r4
    mov.l   r15, @-r4
    mov.l   r14, @-r4
    mov.l   r13, @-r4
    mov.l   r12, @-r4
    mov.l   r11, @-r4
    mov.l   r10, @-r4
    mov.l   r9, @-r4
    mov.l   r8, @-r4
    /* load *to, upwards */
    mov.l   @r5+, r8
    mov.l   @r5+, r9
    mov.l   @r5+, r10
    mov.l   @r5+, r11
    mov.l   @r5+, r12
    mov.l   @r5+, r13
    mov.l   @r5+, r14
    mov.l   @r5+, r15
    lds.l   @r5+, pr
    lds.l   @r5+, mach
    lds.l   @r5+, macl
    ldc.l   @r5+, gbr
    lds.l   @r5+, fpscr
    fmov.s  @r5+, fr12
    fmov.s  @r5+, fr13
    fmov.s  @r5+, fr14
    fmov.s  @r5+, fr15
    rts
    nop

/* A new task starts here: r8 = its function, r9 = its argument. */
    .global coop_trampoline
    .align 2
coop_trampoline:
    mov     r9, r4
    jsr     @r8
    nop
    mov.l   1f, r0
    jsr     @r0             /* returned: never run again */
    nop
    .align 2
1:  .long   coop_exit

    .section .note.GNU-stack, "", @progbits

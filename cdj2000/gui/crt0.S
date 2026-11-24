/* Entry from the boot ROM (0xFFA08000): a stack in L1 scratchpad, then main. */
    .section .text.start
    .global start
start:
    sp.l = 0x0FFC;
    sp.h = 0xFFB0;      /* top of the 4 KB scratchpad */
    fp = sp;
    call _main;
1:  idle;
    jump 1b;

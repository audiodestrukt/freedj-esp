; The CDJ-2000's DSP (TI C6727) program: audio out.  See docs/audio.md.
;
; The main board writes PCM into a ring in our internal RAM through the host
; port (32-bit words, a 16-bit sample in the top half, left then right) and
; moves the write index; we move the read index.  The dMAX streams a
; ping-pong buffer of 2 x 16 stereo frames to McASP1 (the DAC) on its
; transmit event 6, and silence to McASP2 on event 8 (the two run together);
; each finished half raises its transfer-complete code (TCC 2, TCC 4) in
; DTCR0, and we refill the half that just played from the ring, then clear
; the code.  The dMAX's interrupt (INT8) is enabled with a handler that only
; returns: it wakes the core (and lets the emulator skip our wait), and the
; loop does the work.
;
; Memory (internal RAM; the host port's window starts at 0x10000000):
;   0x10002000  this program: its interrupt vectors first (ISTP), entered at the
;               reset vector, which jumps to _start
;   0x10006000  ping-pong buffer for McASP1: half 0, then half 1 at +0x80
;   0x10006100  256 bytes of silence for McASP2
;   0x10007F00  control: +0 magic 'FDJ1' once running, +4 write index (main
;               board), +8 read index (ours), +12 halves played
;   0x10008000  the ring: 8192 words, 4096 frames (93 ms)

        .equ    DTCR0, 0x60000080
        .equ    DEER, 0x6000000C
        .equ    DEHPR, 0x60000014
        .equ    PARAM0, 0x61008000         ; MAX0's parameter RAM: event entries, then transfer entries
        .equ    TE6, 0x61008100         ; event 6's transfer entry (word offset 0x40)
        .equ    TE8, 0x61008140         ; event 8's (word offset 0x50)
        .equ    MCASP1, 0x45000000
        .equ    MCASP2, 0x46000000
        .equ    PP, 0x10006000
        .equ    SILENCE, 0x10006100
        .equ    CTL, 0x10007F00
        .equ    RING, 0x10008000
; Event entry: general purpose (ETYPE 3), PTE, 32-bit elements (ESIZE 2),
; RLOAD ping-pong, TCINT, TCC.  (TI SPRU795, the dMAX event entry.)
        .equ    EV6, 0x02524003         ; TCC 2, PTE 0x40
        .equ    EV8, 0x04525003         ; TCC 4, PTE 0x50
        .equ    COUNTS, 0x00012001         ; COUNT2 1, COUNT1 32 (words), COUNT0 1 (one word per McASP request)

        .text
        .global _start
; Interrupt vectors, 32 bytes each (ISTP points here).  Reset jumps to
; _start; every other one just returns.
vectors:
        b       .S1     _start
        nop     5
        .align  5
        .rept   15
        b       .S2     irp
        nop     5
        .align  5
        .endr

_start:
        ; --- control block: zero, then the magic once everything is set ---
        mvkl    .S1     CTL, A13
        mvkh    .S1     CTL, A13
        zero    .L1     A0
        stw     .D1T1   A0, *+A13[0]
        stw     .D1T1   A0, *+A13[1]
        stw     .D1T1   A0, *+A13[2]
        stw     .D1T1   A0, *+A13[3]

        ; --- silence in both buffers (128 words from PP) ---
        mvkl    .S1     PP, A6
        mvkh    .S1     PP, A6
        mvk     .S2     128, B0
clear:  stw     .D1T1   A0, *A6++
        sub     .L2     B0, 1, B0
  [B0]  b       .S1     clear
        nop     5

        ; --- dMAX transfer entries ---
        mvkl    .S1     TE6, A4
        mvkh    .S1     TE6, A4
        mvkl    .S1     PP, A5              ; source: half 0
        mvkh    .S1     PP, A5
        mvkl    .S1     MCASP1, A7          ; destination: the McASP's data port
        mvkh    .S1     MCASP1, A7
        mvkl    .S2     ret1, B3
        mvkh    .S2     ret1, B3
        b       .S1     entry
        nop     5
ret1:   mvkl    .S1     TE8, A4
        mvkh    .S1     TE8, A4
        mvkl    .S1     SILENCE, A5
        mvkh    .S1     SILENCE, A5
        mvkl    .S1     MCASP2, A7
        mvkh    .S1     MCASP2, A7
        mvkl    .S2     ret2, B3
        mvkh    .S2     ret2, B3
        b       .S1     entry
        nop     5

        ; --- event entries 6 and 8, high priority (MAX0), enabled ---
ret2:   mvkl    .S1     PARAM0, A4
        mvkh    .S1     PARAM0, A4
        mvkl    .S1     EV6, A0
        mvkh    .S1     EV6, A0
        stw     .D1T1   A0, *+A4[6]
        mvkl    .S1     EV8, A0
        mvkh    .S1     EV8, A0
        stw     .D1T1   A0, *+A4[8]
        mvkl    .S1     0x140, A0           ; events 6 and 8
        mvkl    .S1     DEHPR, A4
        mvkh    .S1     DEHPR, A4
        stw     .D1T1   A0, *A4
        mvkl    .S1     DEER, A4
        mvkh    .S1     DEER, A4
        stw     .D1T1   A0, *A4

        ; --- release both McASPs' transmitters (XGBLCTL: clocks, serialisers,
        ;     state machine, frame sync).  The McASP's own set-up (clocks,
        ;     format, pins) is not done yet: see docs/audio.md. ---
        mvkl    .S1     0x1F00, A0
        mvkl    .S1     MCASP1 + 0xA0, A4
        mvkh    .S1     MCASP1 + 0xA0, A4
        stw     .D1T1   A0, *A4
        mvkl    .S1     MCASP2 + 0xA0, A4
        mvkh    .S1     MCASP2 + 0xA0, A4
        stw     .D1T1   A0, *A4

        ; --- the dMAX's interrupt on: ISTP, IER (NMIE + INT8), CSR GIE ---
        mvkl    .S1     vectors, A0
        mvkh    .S1     vectors, A0
        mvc     .S2X    A0, ISTP
        mvk     .S1     0x102, A0
        mvc     .S2X    A0, IER
        mvc     .S2     CSR, B1
        or      .L2     1, B1, B1
        mvc     .S2     B1, CSR

        ; --- running ---
        mvkl    .S1     0x314A4446, A0      ; "FDJ1"
        mvkh    .S1     0x314A4446, A0
        stw     .D1T1   A0, *+A13[0]

        mvkl    .S1     DTCR0, A10
        mvkh    .S1     DTCR0, A10
        mvkl    .S1     TE6 + 8, A11        ; event 6's active count word: its PP bit
        mvkh    .S1     TE6 + 8, A11
        mvkl    .S1     RING, A12
        mvkh    .S1     RING, A12
        mvkl    .S1     8191, A7            ; ring index mask
        mvkh    .S1     8191, A7

loop:   ldw     .D1T1   *A10, A8            ; DTCR0
        nop     4
        extu    .S1     A8, 29, 31, A1      ; TCC 2: McASP1's half played
  [!A1] b       .S1     not6
        nop     5

        ; Refill the half that just played: with PP set the dMAX is now on
        ; half 1, so half 0 is free; with it clear, half 1.
        ldw     .D1T1   *A11, A2
        nop     4
        mvkl    .S1     PP, A6
        mvkh    .S1     PP, A6
        shru    .S1     A2, 31, A2
  [!A2] addk    .S1     0x80, A6
        ldw     .D1T1   *+A13[1], A4        ; the main board's write index
        ldw     .D1T1   *+A13[2], A3        ; our read index
        nop     4
        mvk     .S2     32, B0              ; 16 frames, two words each
copy:   cmpeq   .L1     A3, A4, A1          ; empty: silence
  [A1]  zero    .L1     A5
  [!A1] ldw     .D1T1   *+A12[A3], A5
        nop     4
        stw     .D1T1   A5, *A6++
  [!A1] add     .L1     1, A3, A3
        and     .L1     A3, A7, A3
        sub     .L2     B0, 1, B0
  [B0]  b       .S1     copy
        nop     5
        stw     .D1T1   A3, *+A13[2]
        ldw     .D1T1   *+A13[3], A5        ; halves played
        nop     4
        add     .L1     1, A5, A5
        stw     .D1T1   A5, *+A13[3]
        mvk     .S1     4, A5               ; done with TCC 2
        stw     .D1T1   A5, *A10

not6:   extu    .S1     A8, 27, 31, A1      ; TCC 4: McASP2's half (silence: nothing to refill)
  [!A1] b       .S1     loop
        nop     5
        mvk     .S1     16, A5
        stw     .D1T1   A5, *A10
        b       .S1     loop
        nop     5

; A transfer entry at A4: source A5 (half 0; half 1 at +0x80), destination
; A7, 32 one-word frames a half, reloading in ping-pong.  Word layout (TI
; SPRU795, general-purpose transfer entry): 0 source, 1 destination, 2 PP +
; counts, 3..5 index pairs (destination 31:16, source 15:0) for each
; dimension, 6 reference counts, 7/8 and 9/10 the two reload address sets.
entry:  mvkl    .S1     COUNTS, A0
        mvkh    .S1     COUNTS, A0
        stw     .D1T1   A5, *+A4[0]
        stw     .D1T1   A7, *+A4[1]
        stw     .D1T1   A0, *+A4[2]
        zero    .L1     A1
        stw     .D1T1   A1, *+A4[3]
        mvk     .S1     1, A1               ; the next word of the source per frame
        stw     .D1T1   A1, *+A4[4]
        zero    .L1     A1
        stw     .D1T1   A1, *+A4[5]
        stw     .D1T1   A0, *+A4[6]
        stw     .D1T1   A5, *+A4[7]         ; reload set used after half 1: half 0
        stw     .D1T1   A7, *+A4[8]
        addk    .S1     0x80, A5
        stw     .D1T1   A5, *+A4[9]         ; reload set used after half 0: half 1
        stw     .D1T1   A7, *+A4[10]
        b       .S2     B3
        nop     5

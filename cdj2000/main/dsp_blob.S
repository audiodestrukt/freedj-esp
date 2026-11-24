/* The DSP's program (dsp/audio.s, assembled by GNU binutils for the C6000). */
    .section .rodata
    .global dsp_image, dsp_image_end
    .align 4
dsp_image:
    .incbin "build/dsp.bin"
dsp_image_end:
    .section .note.GNU-stack, "", @progbits

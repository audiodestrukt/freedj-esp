# Audio: the DSP, the dMAX and the DAC

The CDJ-2000's sound comes from its DSP, a **TI TMS320C6727** (C67x+,
floating point), which sends 44.1 kHz stereo over **McASP1** to a Wolfson
**WM8740** DAC. The main board reaches the DSP through its host port. As the
emulators model it (cdj-nxs2-qemu `hw/cdj/boards/cdj2000/dsp_c6727.c`,
`dsp_dmax.c`, `dsp_host.c`) and TI documents the parts (SPRU795, the dMAX;
SPRU878, the McASP; the C6727 datasheet). Our own notes for our own code.

## Booting it (main/dsp.cpp)

- The main board sees a **64 KB window onto DSP memory at 0x0C0C0000**
  (area 3; we use the uncached 0xAC0C0000). At power-on it maps the DSP's
  internal RAM from **0x10000000**; the DSP can move it (CFGHPIAMSB/UMB), we
  don't.
- The DSP's ROM loader takes a program put in that RAM: its **entry at
  0x10000714**, then a non-zero **start word at 0x10000718**. We copy our
  program (`dsp/audio.s`, 0.8 KB) to 0x10002000 and start it there.
- It writes the magic `FDJ1` into its control block once it runs.

## Our DSP program (dsp/audio.s)

Assembled by GNU binutils for the C6000 (`tools/Dockerfile.c6x`), no TI
tools. Its interrupt vectors are first (ISTP), reset jumps to `_start`.

- A **ring of 4096 stereo frames (93 ms)** at 0x10008000, 32-bit words with
  the 16-bit sample in the top half, left then right. The main board writes
  frames and moves the write index (control block +4); the DSP moves the read
  index (+8) and counts the halves it has sent (+12).
- **The dMAX** (MAX0, high-priority events) moves a **ping-pong buffer of 2
  × 16 frames** at 0x10006000 to McASP1's data port on its transmit event 6,
  one word per request, reloading the other half when one ends (RLOAD with
  PP), and raises **TCC 2** in DTCR0. Event 8 does the same for McASP2 with
  silence (TCC 4): the two transmitters run as a pair.
- The program waits for a TCC, refills the half that just played from the
  ring (silence if the ring is empty: a pause is just the main board not
  writing), and clears the TCC. The dMAX's interrupt (INT8) is enabled with a
  handler that only returns, so the core can wait for it.
- ⚠ **The McASPs are only released (XGBLCTL), not set up**: clock dividers,
  the I2S frame and the pins are left at reset values, because the emulator
  does not model them and the board's clocking (who drives the bit clock,
  which serialiser goes to the DAC) is not known yet. That is the first thing
  to settle on a real deck.

## The player (main/player.cpp)

A stand-in until freedj-esp's engine is brought over: the first 16-bit 44.1
kHz stereo WAV on the stick, PLAY/PAUSE, CUE back to the start, fed into the
ring from the main loop. MP3 and the deck's pitch come with the engine.

## Running it on the emulator

- `CDJ_DSP_AUDIO=1` sends McASP1 to the host's audio (`-audio pa` or another
  backend); the sink reports frames in/out every 5 s.
- `CDJ_C6727_IDLE=<our loop>:0:0xffffffff:4` lets the emulator skip our wait
  loop (the build writes its address to `build/dsp.idle`); without it the
  interpreted DSP falls behind real time (about 36 000 frames a second here).
- `tools/test_audio.py` checks it end to end: real-time rate, the exact
  frames in the DAC's buffer, pause, cue.

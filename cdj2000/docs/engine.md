# The deck engine

`main/engine.cpp`: the CDJ-2000's transport, written after the P4 deck's
(`p4/deck/main/main.cpp`, its `apply()` and renderers) for this board's one
loop, on code shared unchanged with the ESP32 decks:

- **`p4/deck/main/stream.cpp`** decodes the track (WAV, or MP3 through
  minimp3) into a 32 MB window round the playhead (three minutes), so cues
  and the jog inside it are instant. It reads the stick through stdio
  (`main/files.cpp`) and allocates through the `esp_heap_caps.h` shim.
- **`src/wsola.cpp`** stretches time for MASTER TEMPO.
- **`p4/deck/main/rekordbox.cpp`** gives the beat grid and the cues (memory
  and hot) from the track's ANLZ file.

## The deck

| Control | What it does |
|---|---|
| PLAY/PAUSE | play, pause (at once: the DSP's ring is dropped) |
| CUE | playing: back to the cue point and pause; paused: the cue point here (to the beat, quantized), and play while held |
| HOT CUE A/B/C | empty: stored here (or the active loop); set: play from it |
| LOOP IN / OUT, RELOOP/EXIT, 4-BEAT LOOP | a loop, to the beat |
| CALL ½× / 2× | the previous / next memory cue |
| MEMORY / DELETE | store / delete a memory cue (this session only) |
| tempo slider, TEMPO RANGE, TEMPO RESET | ±6/10/16 % or WIDE; 0 % while TEMPO RESET is on |
| MASTER TEMPO | keylock: WSOLA instead of varispeed |
| jog | playing: a bend that decays; paused: a scratch the playhead chases |
| DIRECTION lever | reverse (varispeed) |
| select knob, its push | browse the stick's tracks (on the console), load one |

On load the deck is cued at the first memory cue, else the first beat.

⚠ The slider's direction (which end is faster) and the jog's counts per turn
(about 3240, from the emulator's panel scale) are to be checked on a deck.

## On Link

`prolink::publish()` every pass, as the P4 does it: loaded, playing, the
pitch, the track's BPM at the heard position, the beat number and its place
in the bar, and when the next beat will be heard (the playhead less what is
queued for the DAC: the DSP's ring and ping-pong buffer, and WSOLA's lag), so
the beat packets go out on the beat. `tools/test_deck_link.py` catches them on
the deck's private segment: 500.0 ms apart at 120 BPM, 493.7 ms at +1.25 %.

## Tests

- `tools/test_audio.py`: real time, bit exact, pause, resume, the tempo
  slider (+1.25 % moves the track 5061 ms in 5 s), CUE.
- `tools/test_deck_link.py`: an MP3 (when ffmpeg is here) played, its beats on Link.

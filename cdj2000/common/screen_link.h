/* The link from the main board to the display processor (SPORT1): what the
 * main board sends for the deck's screen.  Shared by main/ (SH-4) and gui/
 * (Blackfin), both little-endian.  See docs/screen.md.
 *
 * Every packet is 512 bytes (a multiple of the link's 16-byte unit; the
 * display processor arms its receive for exactly that): a header, then the
 * payload of its type. */
#pragma once
#include <stdint.h>

#define SL_PACKET   512
#define SL_MAGIC    0x4446u             /* "FD" */
#define SL_PAYLOAD  (SL_PACKET - 8)

enum {
    SL_STATE = 1,                       /* the deck now (~30 a second) */
    SL_TRACK = 2,                       /* a track loaded: its title, length, grid */
    SL_PEAKS = 3,                       /* waveform peaks, a run of them */
};

typedef struct {
    uint16_t magic, type;
    uint16_t seq, len;                  /* len: payload bytes used */
} sl_header;

/* Flags in sl_state. */
#define SL_LOADED   0x0001
#define SL_PLAYING  0x0002
#define SL_KEYLOCK  0x0004
#define SL_LOOPING  0x0008
#define SL_REVERSE  0x0010
#define SL_RESET    0x0020              /* TEMPO RESET */

typedef struct {
    uint32_t pos_ms;                    /* the playhead as heard */
    uint32_t cue_ms;
    uint32_t loop_in_ms, loop_out_ms;
    int32_t  speed_ppm;                 /* playback speed, parts per million (1000000 = +0 %) */
    int16_t  tempo_c;                   /* the slider's tempo, hundredths of a percent */
    uint16_t bpm_c;                     /* BPM at the playhead x 100 (with the tempo), 0 = none */
    uint16_t flags;
    uint8_t  beat_in_bar;               /* 1-4, 0 = no grid */
    uint8_t  range;                     /* tempo range in percent: 6, 10, 16, 100 (WIDE) */
    uint32_t hot_ms[3];                 /* hot cues A-C, 0xFFFFFFFF = not set */
} sl_state;

typedef struct {
    uint32_t length_ms;
    uint32_t first_beat_ms;             /* the grid: its first beat and */
    uint32_t beat_us;                   /* beat length in microseconds (0 = no grid) */
    uint32_t peak_frames;               /* audio frames per peak */
    uint32_t rate;                      /* the track's sample rate */
    uint32_t peaks;                     /* how many peaks the whole track has */
    char     title[64];                 /* NUL-terminated, ASCII */
} sl_track;

typedef struct {
    uint32_t first;                     /* the index of the first peak here */
    uint16_t count;                     /* up to SL_PEAKS_MAX */
    uint16_t pad;
    uint8_t  v[SL_PAYLOAD - 8];         /* 0-255: the louder channel's peak */
} sl_peaks;
#define SL_PEAKS_MAX (SL_PAYLOAD - 8)

/* The deck's screen, drawn into a 16-bit framebuffer: shared by the
 * CDJ-2000's display processor (gui/gui.c, the Blackfin, RGB555 at 480x234)
 * and the XDJ-1000MK2's main board (main/screen_lcdc.cpp, the SH7724's
 * LCDC, RGB565 at 800x480).  One design; each screen gives its layout, its
 * fonts (tools/font.py bakes a set per screen into its build's font.h) and
 * its pixel format (DD_RGB565 defined: RGB565, else RGB555).
 *
 * What it shows: the title and PAUSE/REV/LOOP; the waveform round a fixed
 * playhead with the beat grid, the cue, hot cues A-C and the loop; the whole
 * track's overview with where the playhead is; REMAIN, the beat in the bar,
 * TEMPO and its range, MASTER TEMPO, TEMPO RESET and BPM.  See docs/screen.md. */
#pragma once
#include <stdint.h>
#include "screen_link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int w, h;                               /* the framebuffer (its line is w pixels) */
    int top_h, title_x, title_y, flag_y;    /* the title bar */
    int head_x, wave_y0, wave_y1;           /* the waveform: the playhead's x, its band */
    int ms_per_px;                          /* the waveform's zoom */
    int tick_bar, tick_beat;                /* grid ticks' length at the band's edges */
    int cue_tri;                            /* the cue triangle's height */
    int hot_h, hot_w, hot_dx, hot_dy;       /* a hot cue's bar, and its letter from the bar */
    int ov_y0, ov_y1, ov_mark;              /* the overview's band, the playhead marker's overhang */
    int info_y;                             /* the readouts' row */
    int remain_x, big_dy;                   /* REMAIN's x, the big numbers below their labels */
    int beat_x, beat_step, beat_box, beat_dy;   /* the four beat boxes */
    int tempo_x, tempo_dy, mt_dy, reset_dy; /* TEMPO and what goes under it */
    int margin;                             /* from the screen's sides */
} dd_layout;

typedef struct {
    uint16_t *fb;                           /* the buffer drawn into */
    const dd_layout *l;
    const sl_state *st;
    const sl_track *tr;
    const uint8_t *peaks;                   /* the track's peaks, 0-255, tr->peaks of them */
    uint32_t peaks_max;                     /* how many the buffer holds */
    int have_track;
} dd_frame;

/* The CDJ-2000's 480x234 and the XDJ-1000MK2's 800x480. */
extern const dd_layout dd_cdj2000, dd_xdj1000mk2;

/* One frame at playhead `pos_ms` (the caller runs it on between updates). */
void dd_draw(const dd_frame *f, uint32_t pos_ms);

#ifdef __cplusplus
}
#endif

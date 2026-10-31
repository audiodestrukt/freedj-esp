#pragma once
// rekordbox analysis on a USB stick exported by rekordbox. Each analysed track
// has PIONEER/USBANLZ/Pxxx/xxxxxxxx/ANLZ0000.DAT holding its beat grid (PQTZ)
// and the path of its audio file (PPTH), so the stick can be listed and
// synced from without parsing export.pdb.
// Format reference: Deep Symmetry's djl-analysis ("Track Analysis Files").
// Plain C++ and stdio: builds on the PC too, for testing.
#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

namespace rekordbox {

struct Beat {
    uint32_t ms;       // time from track start
    uint16_t bpm100;   // tempo at this beat, BPM x 100
    uint8_t  inBar;    // 1..4
};

// Read one ANLZ file. `audioRel` gets the PPTH path ("/Contents/..."),
// `beats` the grid (PQTZ), `preview` the overview waveform (PWAV: 400
// columns, each byte = height in the low 5 bits (0..31) and whiteness in the
// top 3). Any may be null. False if it isn't an ANLZ file.
bool readAnlz(const char* path, std::string* audioRel, std::vector<Beat>* beats,
              std::vector<uint8_t>* preview = nullptr);

// Every analysed track on media mounted at `root` (e.g. "/usb"), reported
// one at a time: absolute audio path, absolute ANLZ path, BPM at the first
// beat (0 = no grid). Return false from the callback to stop.
using TrackFn = std::function<bool(const char* audio, const char* anlz, float bpm)>;
void scan(const char* root, const TrackFn& fn);

// A loaded track's analysis: the beat grid, plus the overview waveform.
// Beat grid maths: times are in ms from the track start.
struct Grid {
    std::vector<Beat> beats;
    std::vector<uint8_t> preview;   // PWAV columns, empty if none
    bool  empty() const { return beats.size() < 2; }
    // Beats since the first grid beat, fractional (negative before it).
    double beatAt(double ms) const;
    // Track time of a (fractional) beat number; inverse of beatAt.
    double msAt(double beat) const;
    // Tempo around `ms`, BPM (unpitched).
    float  bpmAt(double ms) const;
    // Position in the bar of beat index i (0-based): 1..4.
    int    inBarAt(long i) const;
};

} // namespace rekordbox

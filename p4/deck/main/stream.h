#pragma once
// Streams a WAV from SD through a PSRAM window around the playhead.
// A background task (service()) keeps ~25 s behind and the rest of the
// window ahead filled; the audio task reads frames with get(). Track length
// doesn't matter, and jog/cue/short seeks inside the window are instant.
// A seek outside the window restarts it there (a brief gap while it refills).
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

class TrackStream {
public:
    bool begin(size_t windowBytes);   // allocate the PSRAM window once

    // Loader-task side.
    bool open(const char* path);      // parse header, start filling at frame 0
    void close();
    bool service();                   // refill a chunk; false = nothing to do
    size_t buffered() const { return winEnd_ - winStart_; }

    // Audio-task side.
    bool   isOpen() const { return open_; }
    size_t frames() const { return frames_; }
    uint32_t sampleRate() const { return rate_; }
    const char* name() const { return name_; }
    float  seconds() const { return rate_ ? (float)frames_ / rate_ : 0; }
    void   setPlayhead(size_t frame) { playhead_ = frame; }
    // Copy `n` stereo frames starting at `frame`. False if any are outside
    // the filled window (caller plays silence and waits).
    bool get(size_t frame, int16_t* lr, size_t n = 1) const;

private:
    int16_t* ring_ = nullptr;         // interleaved stereo, cap_ frames
    size_t   cap_ = 0;
    FILE*    f_ = nullptr;
    long     dataOffset_ = 0;
    uint16_t channels_ = 2;
    uint32_t rate_ = 0;
    size_t   frames_ = 0;
    size_t   filePos_ = 0;            // frame the file pointer is at
    char     name_[64] = {};
    std::atomic<bool>   open_{false};
    std::atomic<size_t> winStart_{0}, winEnd_{0}, playhead_{0};
};

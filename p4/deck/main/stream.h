#pragma once
// Streams a WAV or MP3 from SD through a PSRAM window around the playhead.
// A background task (service()) keeps ~25 s behind and the rest of the
// window ahead filled; the audio task reads frames with get(). Track length
// doesn't matter, and jog/cue/short seeks inside the window are instant.
// A seek outside the window restarts it there (a brief gap while it refills).
#include <atomic>
#include <stddef.h>
#include <stdint.h>

class TrackStream {
public:
    bool begin(size_t windowBytes);   // allocate the PSRAM window once

    // Loader-task side.
    bool open(const char* path);      // WAV or MP3 by extension; start filling at frame 0
    void close();
    bool service();                   // refill a chunk; false = nothing to do
    size_t buffered() const { return winEnd_ - winStart_; }
    // Frames decoded ahead of the playhead.
    size_t ahead() const { const size_t e = winEnd_, p = playhead_; return e > p ? e - p : 0; }

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

    // Waveform peaks, filled in by the loader as it decodes: one byte (0..255,
    // peak |sample| of either channel) per kPeakFrames frames of the track,
    // indexed by absolute frame / kPeakFrames. 0 = silent or not decoded yet.
    // Safe to read from any task.
    static constexpr size_t kPeakFrames = 128;
    size_t  peakBins() const { return peakBins_; }
    uint8_t peak(size_t bin) const { return bin < peakBins_ ? peaks_[bin] : 0; }

    // A decoder that produces 16-bit frames sequentially and can seek by frame.
    struct Source {
        virtual ~Source() = default;
        virtual bool   seek(size_t frame) = 0;
        virtual size_t read(int16_t* out, size_t frames) = 0;  // `channels` per frame
        uint16_t channels = 2;
        uint32_t rate = 0;
        size_t   frames = 0;
    };

private:
    int16_t* ring_ = nullptr;         // interleaved stereo, cap_ frames
    size_t   cap_ = 0;
    Source*  src_ = nullptr;
    uint32_t rate_ = 0;
    size_t   frames_ = 0;
    size_t   srcPos_ = 0;             // frame the source will decode next
    char     name_[64] = {};
    uint8_t* peaks_ = nullptr;        // PSRAM, peakCap_ bins (allocated once)
    size_t   peakCap_ = 0;
    std::atomic<size_t> peakBins_{0};
    std::atomic<bool>   open_{false};
    std::atomic<size_t> winStart_{0}, winEnd_{0}, playhead_{0};
};

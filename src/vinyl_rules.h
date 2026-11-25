// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Timecode vinyl: when the record has the deck, and how the deck keeps to
// the needle. Pure logic over the decoder, no hardware, so the host tests
// (native/vinyl_rules_test.cpp) run it; the P4 deck calls it from its vinyl
// task (p4/deck/main/vinyl.cpp) and its audio loop (main.cpp).
//
// The same rules as freedj-3000's DVS (crates/timecode/src/vinyl.rs), where
// three of them were fixed on a turntable (2026-11-24):
// - A hand holding the platter between strokes is not a needle lift: a lift
//   is 500 ms without carrier (150 ms made every back-cue stop go silent and
//   then jump), and a lift only clears the decoder's bit history
//   (Decoder::relock), so the next position comes in ~45 cycles.
// - ABS jumps only on a needle drop, the first lock, or a >=300 ms gap once
//   the record has played steadily for 300 ms. Smaller gaps, once steady,
//   close by bending the speed (<=2 %); while the platter is handled the deck
//   follows speed alone (the old 25 ms / 200 ms re-sync jumped all through a
//   back-cue).
#include <math.h>
#include <stdint.h>
#include "timecode.h"

namespace vinyl_rules {

// ── The record: armed, signal, settling ─────────────────────────────────────

constexpr int16_t kMinPeak     = 600;     // about -35 dBFS
constexpr float   kMaxSpeed    = 3.0f;
constexpr float   kMinSpeed    = 0.02f;
constexpr int     kArmMs       = 300;     // steady carrier near 1x this long arms it
constexpr int     kDropMs      = 40;      // no carrier this long = record stopped
constexpr int     kLiftMs      = 500;     // no carrier this long = needle lifted
constexpr int     kSettleMaxMs = 1000;

struct Record {
    bool     armed = false;
    bool     signal = false;
    bool     settling = false;
    float    speed = 0;          // signed; 0 with no signal or while settling
    uint32_t drops = 0;          // needle drops placed (settling ended on a lock)
    // Events of this window, for logging.
    bool     landed = false;     // a needle drop began
    bool     placed = false;     // settling ended with a position
    bool     gaveUp = false;     // settling ended without one
    bool     armedNow = false;
    int      settleMs = 0;
};

class Window {
public:
    // Once per ~10 ms of input: now, that window's loudest sample, the
    // decoder that has just read it, and whether needle drops settle (ABS).
    const Record& step(int64_t nowUs, int16_t windowPeak, timecode::Decoder& dec, bool absolute) {
        r_.landed = r_.placed = r_.gaveUp = r_.armedNow = false;
        const float sp = dec.speed();
        const float m = fabsf(sp);
        const bool carrier = windowPeak >= kMinPeak && m >= kMinSpeed && m <= kMaxSpeed;
        const int64_t gapUs = haveCarrier_ ? nowUs - lastCarrierUs_ : INT64_MAX;
        if (carrier) { lastCarrierUs_ = nowUs; haveCarrier_ = true; }
        const bool was = r_.signal;
        r_.signal = haveCarrier_ && nowUs - lastCarrierUs_ < kDropMs * 1000;
        if (r_.signal && !was && gapUs >= (int64_t)kLiftMs * 1000 && absolute) {
            r_.settling = true;
            r_.landed = true;
            landedUs_ = nowUs;
            dec.relock();
        }
        if (r_.settling) {
            r_.settleMs = (int)((nowUs - landedUs_) / 1000);
            if (dec.locked() && dec.position() >= 0) {
                r_.settling = false;
                r_.placed = true;
                r_.drops++;
            } else if (r_.settleMs >= kSettleMaxMs || !r_.signal) {
                r_.settling = false;
                r_.gaveUp = true;
            }
        }
        if (carrier && m > 0.6f && m < 1.5f) {   // 33 or 45 rpm
            if (!steadySinceUs_) steadySinceUs_ = nowUs ? nowUs : 1;
            if (!r_.armed && nowUs - steadySinceUs_ >= (int64_t)kArmMs * 1000) { r_.armed = true; r_.armedNow = true; }
        } else {
            steadySinceUs_ = 0;
        }
        r_.speed = r_.signal && !r_.settling ? sp : 0.0f;
        return r_;
    }
    void forget() { *this = Window{}; }
    const Record& record() const { return r_; }

private:
    Record  r_;
    int64_t lastCarrierUs_ = 0;
    bool    haveCarrier_ = false;
    int64_t steadySinceUs_ = 0;
    int64_t landedUs_ = 0;
};

// ── ABS: keeping the deck on the needle ─────────────────────────────────────

constexpr double kSeekMs     = 300.0;   // a gap this big jumps (once steady)
constexpr double kSeekGapMs  = 250.0;   // at most this often
constexpr double kSteadyMs   = 300.0;   // 0.5-2x, the reading within 3 % of the smoothed speed
constexpr double kDeadMs     = 4.0;     // a gap under this is left alone
constexpr double kBendTauMs  = 800.0;   // the bend closes a gap this fast...
constexpr double kBendMax    = 0.02;    // ...at most this much

struct FollowOut {
    bool   seek = false;
    double seekMs = 0;      // where to jump (ms of track)
    bool   drop = false;    // the jump is a needle drop (or the first lock)
    float  bend = 0;        // add to the deck's speed
};

class Follow {
public:
    // One block of dtMs at nowMs: the record's raw and the deck's smoothed
    // speed; the needle's target (ms of track, the output lead included)
    // when ABS has a position; the deck's playhead (ms).
    FollowOut step(float raw, float smooth, double nowMs, double dtMs, bool hasTarget, double targetMs,
                   double deckMs, uint32_t drops) {
        FollowOut o;
        const double m = fabs((double)raw);
        const bool steady = m >= 0.5 && m <= 2.0 && fabs((double)raw - (double)smooth) <= 0.03 * m;
        steadyMs_ = steady ? steadyMs_ + dtMs : 0.0;
        if (!hasTarget) return o;
        const double err = targetMs - deckMs;       // > 0: the deck is behind the needle
        const bool dropped = drops != dropsSeen_;
        dropsSeen_ = drops;
        bool jump;
        if (dropped || !placed_) jump = fabs(err) >= kDeadMs;
        else jump = steadyMs_ >= kSteadyMs && fabs(err) >= kSeekMs && nowMs - lastSeekMs_ >= kSeekGapMs;
        o.drop = dropped || !placed_;
        placed_ = true;
        if (jump) {
            o.seek = true;
            o.seekMs = targetMs < 0 ? 0 : targetMs;
            lastSeekMs_ = nowMs;
        } else {
            o.drop = false;
            if (steadyMs_ >= kSteadyMs && fabs(err) >= kDeadMs) {
                double b = err / kBendTauMs;
                if (b > kBendMax) b = kBendMax;
                if (b < -kBendMax) b = -kBendMax;
                o.bend = (float)(b * m);
            }
        }
        return o;
    }
    // The record lost the deck: the next position places it afresh.
    void forget() { *this = Follow{}; }

private:
    double   steadyMs_ = 0;
    bool     placed_ = false;
    uint32_t dropsSeen_ = 0;
    double   lastSeekMs_ = -1e12;
};

} // namespace vinyl_rules

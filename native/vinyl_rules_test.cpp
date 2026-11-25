// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for the timecode vinyl rules (src/vinyl_rules.h) and the
// decoder fixes they rely on (src/timecode.cpp), with a synthetic Serato
// Control Vinyl: a quadrature 1 kHz carrier whose loud/quiet cycles spell
// the record's LFSR sequence, so position, direction and speed are known.
//
//   make vinyl_rules_test && ./vinyl_rules_test     (exit 0 = all pass)
//
// Each case is a fix found on a turntable with freedj-3000's DVS
// (2026-11-24), which runs the same rules.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "timecode.h"
#include "vinyl_rules.h"

namespace {

constexpr int    kSr = 48000;
constexpr double kPi = 3.14159265358979323846;
// Serato Control Vinyl (the format's published constants; as timecode.cpp).
constexpr uint32_t kBits = 20, kSeed = 0x59017, kTaps = 0x361e4, kLength = 712000;
constexpr double kStartCycles = 24000;   // track 0:00 = 24.0 s of timecode on Dan's record

int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { g_pass++; } else { g_fail++; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

uint32_t parity(uint32_t x) { return __builtin_popcount(x) & 1u; }
uint32_t fwd(uint32_t s) { return (s >> 1) | (parity(s & (kTaps | 1u)) << (kBits - 1)); }

// The bit each cycle carries: the MSB of the LFSR state there.
const std::vector<bool>& bits() {
    static std::vector<bool> b;
    if (b.empty()) {
        b.reserve(kLength);
        uint32_t s = kSeed;
        for (uint32_t i = 0; i < kLength; i++) { b.push_back((s >> (kBits - 1)) & 1u); s = fwd(s); }
    }
    return b;
}

// A record under a needle: phase in cycles (1 cycle = 1 bit = 1 ms at 33).
struct Record {
    double phase;
    explicit Record(double cycles) : phase(cycles) {}
    // `frames` of stereo at `speed` (signed), primary (R) = cos, secondary (L) = sin.
    void render(double speed, int frames, std::vector<int16_t>& out) {
        const double step = speed * 1000.0 / kSr;
        for (int i = 0; i < frames; i++) {
            long c = (long)std::floor(phase);
            if (c < 0) c = 0;
            if (c >= (long)kLength) c = kLength - 1;
            const double amp = bits()[c] ? 20000.0 : 14000.0;
            const double a = phase * 2.0 * kPi;
            out.push_back((int16_t)(17000.0 * std::sin(a)));
            out.push_back((int16_t)(amp * std::cos(a)));
            phase += step;
        }
    }
};

// The P4 vinyl task, as on the deck: 10 ms windows through the decoder and
// the Window rules. Time is the input sample count, so every run is the same.
struct Turntable {
    timecode::Decoder dec;
    vinyl_rules::Window win;
    int64_t nowUs = 0;
    bool absolute = true;
    vinyl_rules::Record last;
    Turntable() { dec.begin(kSr, timecode::Format::SeratoControlVinyl); }
    void window(const std::vector<int16_t>& buf) {
        int16_t peak = 0;
        for (int16_t v : buf) { const int16_t a = (int16_t)std::min(32767, std::abs((int)v)); if (a > peak) peak = a; }
        dec.pushFrames(buf.data(), (int)buf.size() / 2);
        nowUs += (int64_t)buf.size() / 2 * 1000000 / kSr;
        last = win.step(nowUs, peak, dec, absolute);
    }
    void play(Record& r, double speed, double secs) {
        std::vector<int16_t> buf;
        for (int i = 0; i < (int)std::lround(secs * 100); i++) { buf.clear(); r.render(speed, kSr / 100, buf); window(buf); }
    }
    void silence(double secs) {
        const std::vector<int16_t> buf(kSr / 100 * 2, 0);
        for (int i = 0; i < (int)std::lround(secs * 100); i++) window(buf);
    }
    double posMs() const { return dec.locked() && dec.position() >= 0 ? dec.position() - kStartCycles : -1; }
};

// ── The decoder ─────────────────────────────────────────────────────────────

void backwards_reads_the_needle() {
    std::printf("backwards reads the needle, not 19 ms ahead\n");
    Turntable t;
    Record r(100000);
    t.play(r, 1.0, 0.5);
    for (double sp : {-1.0, -0.5, -2.0}) {
        t.play(r, sp, 0.5);
        CHECK(t.dec.locked(), "locked at %+.1fx", sp);
        CHECK(std::abs(t.dec.position() - (int)r.phase) <= 1, "at %+.1fx: read %d, the needle is at %d", sp, t.dec.position(), (int)r.phase);
    }
}

void relock_is_quick() {
    std::printf("relock() places a needle drop in ~45 cycles; reset() needs the level again\n");
    Turntable t;
    Record a(50000);
    t.play(a, 1.0, 0.5);
    t.dec.relock();
    Record b(300000);
    int ms = 0;
    for (; ms < 500 && !(t.dec.locked() && t.dec.position() >= 0); ms += 10) t.play(b, 1.0, 0.01);
    CHECK(ms <= 60, "after relock: a position in %d ms (20 bits to fill, 24 to trust)", ms);
    CHECK(std::abs(t.dec.position() - (int)b.phase) <= 2, "at the new place: %d vs %d", t.dec.position(), (int)b.phase);
    t.dec.reset();
    Record c(500000);
    int slow = 0;
    for (; slow < 1000 && !(t.dec.locked() && t.dec.position() >= 0); slow += 10) t.play(c, 1.0, 0.01);
    CHECK(slow > ms, "a full reset is slower (%d ms vs %d ms): relock is what a needle drop wants", slow, ms);
}

// ── The record: arming, lifts, settling ─────────────────────────────────────

void arms_on_a_steady_record_only() {
    std::printf("arms after 0.3 s of a steady 33, never on a wobble\n");
    Turntable t;
    Record r(kStartCycles + 60000);
    t.play(r, 1.0, 0.2);
    CHECK(!t.last.armed, "not after 0.2 s");
    t.play(r, 1.0, 0.2);
    CHECK(t.last.armed, "armed by 0.4 s");
    Turntable w;
    Record m(0);
    for (int i = 0; i < 100; i++) w.play(m, 1.5 + 1.2 * std::sin(i * 0.4), 0.01);
    CHECK(!w.last.armed, "a wobbling tone (music) never arms");
}

void a_hand_holding_the_platter_is_not_a_lift() {
    std::printf("back-cueing: 300 ms holds between strokes never go silent\n");
    Turntable t;
    Record r(kStartCycles + 60000);
    t.play(r, 1.0, 0.6);
    const uint32_t drops = t.last.drops;
    for (double sp : {-1.5, 1.5, -1.0, 2.0}) {
        t.silence(0.3);
        t.play(r, sp, 0.03);
        CHECK(!t.last.settling, "%+.1f: not settling", sp);
        CHECK(std::fabs(t.last.speed - sp) < 0.3, "%+.1f: plays at once (read %+.2f)", sp, t.last.speed);
        t.play(r, sp, 0.2);
    }
    CHECK(t.last.drops == drops, "no needle drops counted (%u -> %u)", drops, t.last.drops);
}

void a_needle_drop_settles_briefly() {
    std::printf("a needle drop: silent until placed, within 60 ms\n");
    Turntable t;
    Record a(kStartCycles + 10000);
    t.play(a, 1.0, 0.6);
    const uint32_t drops = t.last.drops;
    t.silence(0.6);                      // lifted
    Record b(kStartCycles + 200000);
    t.play(b, 1.0, 0.02);
    CHECK(t.last.settling && t.last.speed == 0.0f, "settling, silent");
    t.play(b, 1.0, 0.04);
    CHECK(!t.last.settling, "placed by 60 ms");
    CHECK(t.last.drops == drops + 1, "counted as a drop");
    CHECK(std::fabs(t.posMs() - (b.phase - kStartCycles)) < 3, "at the new place: %.0f vs %.0f", t.posMs(), b.phase - kStartCycles);
}

// ── ABS: keeping the deck on the needle ─────────────────────────────────────

// The P4's audio loop in miniature: 5.8 ms blocks, the deck's speed smoothed
// 0.35 a block, a playhead moved by it, and Follow steering it.
struct Deck {
    vinyl_rules::Follow follow;
    double posMs = 0, nowMs = 0;
    float smooth = 0;
    int jumps = 0, dropJumps = 0;
    double biggestStepMs = 0;
    static constexpr double kBlockMs = 256 * 1000.0 / 44100.0;
    void block(float raw, bool has, double needleMs, uint32_t drops) {
        smooth += 0.35f * (raw - smooth);
        const vinyl_rules::FollowOut f = follow.step(raw, smooth, nowMs, kBlockMs, has, needleMs, posMs, drops);
        const double before = posMs;
        if (f.seek) { posMs = f.seekMs; jumps++; if (f.drop) dropJumps++; }
        else posMs += (smooth + f.bend) * kBlockMs;
        if (!f.seek) biggestStepMs = std::max(biggestStepMs, std::fabs(posMs - before));
        nowMs += kBlockMs;
    }
};

void follow_places_on_first_lock_then_keeps_up() {
    std::printf("ABS: the first lock places the deck; steady play never jumps\n");
    Deck d;
    double needle = 10000;
    d.block(1.0f, true, needle, 0);
    CHECK(d.jumps == 1 && std::fabs(d.posMs - needle) < 1e-6, "placed at the needle");
    for (int i = 0; i < 1000; i++) { needle += Deck::kBlockMs; d.block(1.0f, true, needle, 0); }
    CHECK(d.jumps == 1, "no more jumps in 5.8 s of play (%d)", d.jumps);
    CHECK(std::fabs(d.posMs - needle) < 5, "on the needle: %.1f ms off", d.posMs - needle);
}

void follow_never_jumps_while_back_cueing() {
    std::printf("ABS: rocking the record never jumps\n");
    Deck d;
    double needle = 30000;
    d.block(1.0f, true, needle, 0);
    for (int i = 0; i < 400; i++) {                         // 2.3 s of rocking, ±1.8x
        const float sp = (float)(1.8 * std::sin(i * Deck::kBlockMs / 1000.0 * 6.0));
        needle += sp * Deck::kBlockMs;
        d.block(sp, true, needle + (i % 7) * 15.0, 0);      // a noisy needle position on top
    }
    CHECK(d.jumps == 1, "only the first placement (%d jumps)", d.jumps);
    CHECK(d.biggestStepMs < 12, "the playhead moved only as the hand did (biggest step %.1f ms)", d.biggestStepMs);
}

void follow_jumps_on_drops_bends_small_gaps() {
    std::printf("ABS: a drop jumps; a 40 ms gap bends shut; a big gap under a steady record jumps once\n");
    Deck d;
    double needle = 10000;
    d.block(1.0f, true, needle, 0);
    needle = 60000;
    d.block(1.0f, true, needle, 1);
    CHECK(d.dropJumps == 2 && std::fabs(d.posMs - needle) < 1e-6, "the drop jumped to the needle");
    d.posMs -= 40;                                          // 40 ms behind after a scratch
    bool bent = false;
    for (int i = 0; i < 800; i++) {
        needle += Deck::kBlockMs;
        const double before = d.posMs;
        d.block(1.0f, true, needle, 1);
        if (d.posMs - before > Deck::kBlockMs * 1.001) bent = true;
    }
    CHECK(bent, "closed by bending, not a jump");
    CHECK(d.jumps == 2, "no jump for 40 ms (%d)", d.jumps);
    CHECK(std::fabs(d.posMs - needle) < vinyl_rules::kDeadMs + 1, "closed: %.1f ms off", d.posMs - needle);
    d.posMs += 30000;                                       // moved 30 s under a steady record
    for (int i = 0; i < 5; i++) { needle += Deck::kBlockMs; d.block(1.0f, true, needle, 1); }
    CHECK(d.jumps == 3 && std::fabs(d.posMs - needle) < 10, "one jump back to the needle (%d, %.0f off)", d.jumps, d.posMs - needle);
}

} // namespace

int main() {
    backwards_reads_the_needle();
    relock_is_quick();
    arms_on_a_steady_record_only();
    a_hand_holding_the_platter_is_not_a_lift();
    a_needle_drop_settles_briefly();
    follow_places_on_first_lock_then_keeps_up();
    follow_never_jumps_while_back_cueing();
    follow_jumps_on_drops_bends_small_gaps();
    std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

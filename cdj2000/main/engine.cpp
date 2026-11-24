// The CDJ-2000's deck engine (see engine.h).  The transport follows the P4
// deck's apply() and renderers (p4/deck/main/main.cpp): CUE as on a CDJ
// (back to the cue and pause while playing; from pause set it, quantized,
// and preview while held), hot cues A-C, loops (in/out, reloop, 4-beat),
// memory cue call, the tempo slider with its ranges, TEMPO RESET, MASTER TEMPO
// (WSOLA), the jog (a bend while playing, a scratch while paused), the
// DIRECTION lever.  No SYNC: a CDJ-2000 has none.
#include "engine.h"
#include "stream.h"
#include "wsola.h"
#include "rekordbox.h"
#include "prolink.h"
#include "dsp.h"
#include "tmu.h"
#include "con.h"
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

namespace engine {
namespace {
constexpr double kRate = 44100;                 // the DAC's
constexpr int kBlock = 256;                     // frames rendered at a time
constexpr float kRanges[] = {0.06f, 0.10f, 0.16f, 1.0f};
// The jog's counter: about 3240 counts a turn (the panel's measured scale:
// 72 counts a tick, 25 ticks in the 200 degrees a nominal 33 1/3 rpm turn
// takes a second).  ⚠ To check on a deck.
constexpr float kNudgePerCount = 0.0002f, kNudgeMax = 0.3f, kNudgeDecay = 0.97f;
constexpr double kScrubFramesPerCount = 24.5;   // a turn moves a record's 1.8 s
constexpr double kScratchChase = 4.0 * 256, kScratchMaxSpeed = 8.0;
constexpr int64_t kDecodeBudgetUs = 4000;       // per pass, after the DSP is fed

struct Track { std::string audio, anlz; float bpm; };
std::vector<Track> list;

struct HotCue { bool set = false, loop = false; double start = 0, end = 0; };
struct Deck {
    bool loaded = false, playing = false, previewing = false, keylock = false, quantize = true;
    bool looping = false, loopInSet = false, reverse = false, scratching = false, tempoReset = false;
    double loopStart = 0, loopEnd = 0, loopIn = 0, loopBeats = 0;
    HotCue hot[8];
    std::vector<double> mem;                    // memory cues from the stick (frames), sorted
    float fader = 0.5f, tempo = 1.0f, nudge = 0.0f;
    int range = 1;
    double scratchTarget = 0, pos = 0, cue = 0;
    int selected = 0;
};
Deck d;
uint32_t generation;
TrackStream stream;
wsola::Wsola ws;
rekordbox::Grid grid;
double wsolaLag;

double rate() { return stream.sampleRate() ? stream.sampleRate() : kRate; }
double beatOf(double frame) { return grid.beatAt(frame * 1000.0 / rate()); }
double frameOf(double beat) { return grid.msAt(beat) * rate() / 1000.0; }
double snap(double frame) { return d.quantize && !grid.empty() ? frameOf(floor(beatOf(frame) + 0.5)) : frame; }

void seek(double frame)
{
    if (frame < 0) frame = 0;
    d.pos = frame;
    stream.setPlayhead(static_cast<size_t>(frame));
    ws.reset();
    dsp::flush();                                // heard at once, not after the ring
}

void setLoop(double start, double end, double beats)
{
    d.loopStart = start; d.loopEnd = end; d.loopBeats = beats; d.looping = end > start;
}

void tempoFromFader()
{
    d.tempo = d.tempoReset ? 1.0f : 1.0f + (d.fader - 0.5f) * 2.0f * kRanges[d.range];
}

bool load(int i)
{
    if (i < 0 || i >= static_cast<int>(list.size())) return false;
    const Track &t = list[i];
    d.playing = d.previewing = d.looping = d.loopInSet = d.scratching = false;
    d.loaded = false;
    dsp::flush();
    if (!stream.open(t.audio.c_str())) return false;
    grid = rekordbox::Grid{};
    rekordbox::readAnlz(t.anlz.c_str(), nullptr, &grid.beats, &grid.preview);
    grid.cues = rekordbox::readCues(t.anlz.c_str());
    for (auto &h : d.hot) h = HotCue{};
    d.mem.clear();
    const double r = rate();
    for (const auto &c : grid.cues) {
        if (c.hot >= 1 && c.hot <= 8) {
            HotCue &h = d.hot[c.hot - 1];
            h.set = true; h.loop = c.loop; h.start = c.ms * r / 1000.0; h.end = c.loopMs * r / 1000.0;
        } else if (!c.hot) {
            d.mem.push_back(c.ms * r / 1000.0);
        }
    }
    // The cue: the first memory cue, else the first beat, as a CDJ loads.
    d.cue = !d.mem.empty() ? d.mem.front() : !grid.empty() ? grid.beats.front().ms * r / 1000.0 : 0;
    seek(d.cue);
    d.loaded = true;
    generation++;
    con_printf("deck: loaded %s (%.1f s, %u Hz, %u beats, %u cues)\n", stream.name(), static_cast<double>(stream.seconds()),
               static_cast<unsigned>(stream.sampleRate()), static_cast<unsigned>(grid.beats.size()),
               static_cast<unsigned>(grid.cues.size()));
    return true;
}

// Varispeed: linear interpolation at a fractional step; pitch follows speed.
bool renderPitched(float speed, int16_t *out)
{
    const double step = (d.reverse ? -1 : 1) * speed * rate() / kRate;
    for (int i = 0; i < kBlock; i++) {
        if (d.pos < 0) { d.pos = 0; memset(out + 2 * i, 0, (kBlock - i) * 4); d.playing = false; return true; }
        const size_t i0 = static_cast<size_t>(d.pos);
        if (i0 + 1 >= stream.frames()) { memset(out + 2 * i, 0, (kBlock - i) * 4); d.playing = false; return true; }
        int16_t two[4];
        if (!stream.get(i0, two, 2)) return false;
        const float fr = static_cast<float>(d.pos - i0);
        out[2 * i] = static_cast<int16_t>(two[0] + fr * (two[2] - two[0]));
        out[2 * i + 1] = static_cast<int16_t>(two[1] + fr * (two[3] - two[1]));
        d.pos += step;
        if (d.looping && !d.reverse && d.pos >= d.loopEnd) d.pos -= d.loopEnd - d.loopStart;
        else if (d.looping && d.reverse && d.pos < d.loopStart) d.pos += d.loopEnd - d.loopStart;
    }
    return true;
}

// Scratch (paused jog): the playhead chases the jog's target.
bool renderScratch(int16_t *out)
{
    static int16_t holdL, holdR;
    bool moving = false;
    for (int i = 0; i < kBlock; i++) {
        double step = (d.scratchTarget - d.pos) / kScratchChase;
        if (step > kScratchMaxSpeed) step = kScratchMaxSpeed;
        if (step < -kScratchMaxSpeed) step = -kScratchMaxSpeed;
        if (fabs(d.scratchTarget - d.pos) < 0.5) step = 0;
        if (step != 0) moving = true;
        const size_t i0 = static_cast<size_t>(d.pos);
        int16_t two[4];
        if (step == 0 || !stream.get(i0, two, 2)) { out[2 * i] = holdL; out[2 * i + 1] = holdR; continue; }
        const float fr = static_cast<float>(d.pos - i0);
        out[2 * i] = holdL = static_cast<int16_t>(two[0] + fr * (two[2] - two[0]));
        out[2 * i + 1] = holdR = static_cast<int16_t>(two[1] + fr * (two[3] - two[1]));
        d.pos += step;
        if (d.pos < 0) d.pos = 0;
    }
    return moving;
}

// MASTER TEMPO: the file resampled to 44.1 kHz, then WSOLA to `speed`.
bool renderKeylock(float speed, int16_t *out)
{
    constexpr int kFeed = 512;
    static int16_t feed[kFeed * 2];
    static int16_t raw[(kFeed * 3 + 4) * 2];
    const double step = rate() / kRate;
    ws.setSpeed(speed);
    int got = 0;
    while (got < kBlock) {
        got += ws.pullFrames(out + 2 * got, kBlock - got);
        if (got >= kBlock) break;
        if (d.looping && d.pos >= d.loopEnd) d.pos -= d.loopEnd - d.loopStart;
        const size_t base = static_cast<size_t>(d.pos), total = stream.frames();
        if (base + 1 >= total) { memset(out + 2 * got, 0, (kBlock - got) * 4); d.playing = false; return true; }
        int n = ws.inputCapacity();
        if (n > kFeed) n = kFeed;
        const double frac = d.pos - base;
        const int fit = static_cast<int>((total - 1 - base - frac) / step);
        if (n > fit) n = fit;
        if (d.looping) { const int toEnd = static_cast<int>((d.loopEnd - d.pos) / step) + 1; if (n > toEnd) n = toEnd; }
        if (n <= 0) { memset(out + 2 * got, 0, (kBlock - got) * 4); d.playing = false; return true; }
        const size_t span = static_cast<size_t>(frac + (n - 1) * step) + 2;
        if (!stream.get(base, raw, span)) return false;
        for (int i = 0; i < n; i++) {
            const double x = frac + i * step;
            const size_t j = static_cast<size_t>(x);
            const float f = static_cast<float>(x - j);
            const int16_t *a0 = raw + 2 * j;
            feed[2 * i] = static_cast<int16_t>(a0[0] + f * (a0[2] - a0[0]));
            feed[2 * i + 1] = static_cast<int16_t>(a0[1] + f * (a0[3] - a0[1]));
        }
        d.pos += ws.pushFrames(feed, n) * step;
    }
    return true;
}

float speed() { const float s = d.tempo + d.nudge; return s < 0.05f ? 0.05f : s; }

// Where the listener is, in track ms: the playhead less what is queued for
// the DAC (the ring and the DSP's ping-pong buffer), and WSOLA's lag.
double heardMs()
{
    const float sp = speed();
    double lag = (dsp::queued() + 32) * sp;
    if (d.keylock) lag += wsolaLag;
    return (d.pos - lag * rate() / kRate) * 1000.0 / rate();
}

void publish()
{
    prolink::Ours o;
    o.loaded = d.loaded;
    o.playing = d.playing;
    o.pitch = d.tempo;
    if (d.loaded && !grid.empty()) {
        const double ms = heardMs(), b = grid.beatAt(ms);
        o.trackBpm = grid.bpmAt(ms);
        if (b >= 0) { o.beat = static_cast<uint32_t>(b) + 1; o.beatInBar = static_cast<uint8_t>(grid.inBarAt(static_cast<long>(b))); }
        if (d.playing && !d.reverse) {
            const double next = floor(b) + 1, inMs = (grid.msAt(next) - ms) / speed();
            o.nextBeatUs = tmu_us64() + static_cast<int64_t>(inMs * 1000);
            o.nextBeat = next >= 0 ? static_cast<uint32_t>(next) + 1 : 0;
            o.nextBeatInBar = static_cast<uint8_t>(grid.inBarAt(static_cast<long>(next)));
        }
    }
    prolink::publish(o);
}
} // namespace

void begin()
{
    stream.begin(32u << 20);                     // 32 MB: three minutes around the playhead
    ws.begin(2);
}

void add_track(const char *audio, const char *anlz, float bpm)
{
    list.push_back(Track{audio, anlz, bpm});
    if (list.size() == 1) load(0);
}

int tracks() { return static_cast<int>(list.size()); }

void apply(const deck::Event &e)
{
    using deck::Type;
    switch (e.type) {
    case Type::Browse: {
        const int n = static_cast<int>(list.size());
        if (!n) break;
        d.selected = ((d.selected + e.delta) % n + n) % n;
        con_printf("deck: select %d/%d %s\n", d.selected + 1, n, list[d.selected].audio.c_str());
        break;
    }
    case Type::Load:
        if (e.pressed) load(d.selected);
        break;
    case Type::PlayPause:
        if (e.pressed && d.loaded) {
            d.scratching = false;
            d.playing = !d.playing;
            d.previewing = false;
            if (!d.playing) dsp::flush();        // a CDJ stops at once
        }
        break;
    case Type::Cue:
        if (!d.loaded) break;
        if (e.pressed) {
            if (d.playing) { seek(d.cue); d.playing = false; }
            else { d.cue = snap(d.pos); if (d.cue != d.pos) seek(d.cue); d.previewing = true; d.playing = true; }
        } else if (d.previewing) {
            d.previewing = false; d.playing = false; seek(d.cue);
        }
        break;
    case Type::HotCue: {
        const int i = e.delta - 1;
        if (!e.pressed || !d.loaded || i < 0 || i > 7) break;
        HotCue &h = d.hot[i];
        if (!h.set) {
            h.set = true; h.loop = d.looping;
            h.start = d.looping ? d.loopStart : snap(d.pos);
            h.end = d.looping ? d.loopEnd : 0;
            con_printf("deck: hot cue %c set\n", 'A' + i);
        } else {
            seek(h.start);
            if (h.loop) setLoop(h.start, h.end, 0); else d.looping = false;
            d.playing = true; d.previewing = false;
        }
        break;
    }
    case Type::BeatLoop: {
        if (!e.pressed || !d.loaded || grid.empty()) break;
        const double beats = e.position > 0 ? e.position : 4;
        const double start = d.quantize ? frameOf(floor(beatOf(d.pos) + 1e-6)) : d.pos;
        setLoop(start, frameOf(beatOf(start) + beats), beats);
        break;
    }
    case Type::LoopIn:
        if (e.pressed && d.loaded) { d.loopIn = snap(d.pos); d.loopInSet = true; }
        break;
    case Type::LoopOut:
        if (!e.pressed || !d.loaded) break;
        if (d.loopInSet && snap(d.pos) > d.loopIn) { setLoop(d.loopIn, snap(d.pos), 0); d.loopInSet = false; }
        break;
    case Type::ReloopExit:
        if (!e.pressed || !d.loaded) break;
        if (d.looping) d.looping = false;
        else if (d.loopEnd > d.loopStart) { seek(d.loopStart); setLoop(d.loopStart, d.loopEnd, d.loopBeats); }
        break;
    case Type::CueCall: {
        if (!e.pressed || !d.loaded || d.mem.empty()) break;
        const double eps = 0.05 * rate();
        int pick = -1;
        const int n = static_cast<int>(d.mem.size());
        if (e.delta > 0) { for (int i = 0; i < n; i++) if (d.mem[i] > d.pos + eps) { pick = i; break; } }
        else { for (int i = n - 1; i >= 0; i--) if (d.mem[i] < d.pos - eps) { pick = i; break; } }
        if (pick < 0) break;
        d.cue = d.mem[pick];
        d.looping = false;
        seek(d.cue);
        break;
    }
    case Type::MemoryAdd:
        if (e.pressed && d.loaded) { d.mem.push_back(d.cue); for (size_t i = d.mem.size() - 1; i > 0 && d.mem[i - 1] > d.mem[i]; i--) { double t = d.mem[i]; d.mem[i] = d.mem[i - 1]; d.mem[i - 1] = t; } }
        break;
    case Type::MemoryDelete:
        if (e.pressed) for (size_t i = 0; i < d.mem.size(); i++) if (fabs(d.mem[i] - d.cue) < 0.1 * rate()) { d.mem.erase(d.mem.begin() + i); break; }
        break;
    case Type::TempoFader:
        d.fader = e.position;
        tempoFromFader();
        break;
    case Type::TempoRange:
        if (e.pressed) { d.range = (d.range + 1) % 4; tempoFromFader(); }
        break;
    case Type::TempoReset:
        if (e.pressed) { d.tempoReset = !d.tempoReset; tempoFromFader(); }
        break;
    case Type::KeyLock:
        if (e.pressed) { d.keylock = !d.keylock; ws.reset(); }
        break;
    case Type::Reverse:                          // the DIRECTION lever: down = REV
        if (d.loaded) { d.reverse = e.pressed; ws.reset(); }
        break;
    case Type::Jog:
        if (d.playing) {
            d.nudge += e.delta * kNudgePerCount;
            if (d.nudge > kNudgeMax) d.nudge = kNudgeMax;
            if (d.nudge < -kNudgeMax) d.nudge = -kNudgeMax;
        } else if (d.loaded) {
            if (!d.scratching) { d.scratching = true; d.scratchTarget = d.pos; ws.reset(); }
            d.scratchTarget += e.delta * kScrubFramesPerCount;
            if (d.scratchTarget < 0) d.scratchTarget = 0;
            if (d.scratchTarget > stream.frames()) d.scratchTarget = stream.frames();
        }
        break;
    default:
        break;
    }
}

void service()
{
    // Feed the DSP first, a block at a time while its ring has room.
    static int16_t out[2 * kBlock];
    while (d.loaded && dsp::room() >= kBlock) {
        bool ok;
        if (d.playing) ok = d.keylock && !d.reverse ? renderKeylock(speed(), out) : renderPitched(speed(), out);
        else if (d.scratching) { ok = true; if (!renderScratch(out)) { d.scratching = false; break; } }
        else break;                              // paused: nothing to send, the DAC gets silence
        if (!ok) break;                          // not decoded yet: wait for the stream
        stream.setPlayhead(static_cast<size_t>(d.pos));
        dsp::push(out, kBlock);
        d.nudge *= kNudgeDecay;
        if (d.keylock) {
            const int held = 4096 - ws.inputCapacity() - wsola::Wsola::FRAME_N / 2;
            const double raw = (held > 0 ? held : 0) + ws.outputAvailable() * speed();
            wsolaLag += (raw - wsolaLag) * 0.02;
        }
    }
    // Then decode ahead, for as long as the budget allows.
    const int64_t until = tmu_us64() + kDecodeBudgetUs;
    while (tmu_us64() < until && stream.service()) {}
    publish();
}

View view()
{
    View v{};
    v.loaded = d.loaded;
    v.playing = d.playing;
    v.keylock = d.keylock;
    v.looping = d.looping;
    v.reverse = d.reverse;
    v.tempo_reset = d.tempoReset;
    v.generation = generation;
    v.range_pct = static_cast<uint8_t>(kRanges[d.range] * 100.0f + 0.5f);
    v.tempo = d.tempo;
    v.speed = d.playing ? speed() : 0.0f;
    if (!d.loaded) return v;
    const double r = rate();
    const double heard = heardMs();
    v.pos_ms = heard > 0 ? static_cast<uint32_t>(heard) : 0;
    v.cue_ms = static_cast<uint32_t>(d.cue * 1000.0 / r);
    v.loop_in_ms = static_cast<uint32_t>(d.loopStart * 1000.0 / r);
    v.loop_out_ms = static_cast<uint32_t>(d.loopEnd * 1000.0 / r);
    v.length_ms = static_cast<uint32_t>(stream.seconds() * 1000.0f);
    for (int k = 0; k < 3; k++) v.hot_ms[k] = d.hot[k].set ? static_cast<uint32_t>(d.hot[k].start * 1000.0 / r) : 0xFFFFFFFFu;
    if (!grid.empty()) {
        v.bpm = grid.bpmAt(heard) * d.tempo;
        const double b = grid.beatAt(heard);
        v.beat_in_bar = b >= 0 ? static_cast<uint8_t>(grid.inBarAt(static_cast<long>(b))) : 0;
        v.first_beat_ms = grid.beats.front().ms;
        const float bpm0 = grid.beats.front().bpm100 / 100.0f;
        v.beat_us = bpm0 > 0 ? static_cast<uint32_t>(60e6f / bpm0) : 0;
    }
    v.rate = stream.sampleRate();
    v.peak_frames = TrackStream::kPeakFrames;
    v.peaks = static_cast<uint32_t>(stream.peakBins());
    v.title = stream.name();
    return v;
}

uint8_t peak(uint32_t i) { return stream.peak(i); }

uint32_t peaks_decoded()
{
    // The stream decodes ahead of the playhead it was given: what it holds
    // ends at the playhead plus what is ahead of it.
    const size_t end = static_cast<size_t>(d.pos) + stream.ahead();
    return static_cast<uint32_t>(end / TrackStream::kPeakFrames);
}

bool playing() { return d.playing; }
bool loaded() { return d.loaded; }
uint32_t position_ms() { return d.loaded ? static_cast<uint32_t>(d.pos * 1000.0 / rate()) : 0; }

Lamps lamps()
{
    Lamps l{};
    l.play = d.playing && !d.previewing;
    l.cue = d.loaded && (!d.playing || d.previewing);
    l.loop_in = l.loop_out = d.looping;
    l.reloop = d.loopEnd > d.loopStart;
    l.master_tempo = d.keylock;
    l.tempo_reset = d.tempoReset;
    return l;
}

void summary(char *out, int n)
{
    if (!d.loaded) { snprintf(out, n, "no track"); return; }
    const double ms = d.pos * 1000.0 / rate();
    if (grid.empty()) {
        snprintf(out, n, "%s  %.2f/%.1f s  %+.2f %%  %s", stream.name(), ms / 1000.0, static_cast<double>(stream.seconds()),
                 (d.tempo - 1.0) * 100.0, d.playing ? "playing" : "paused");
        return;
    }
    const double b = grid.beatAt(ms);
    snprintf(out, n, "%s  %.2f/%.1f s  %+.2f %%  %.2f BPM  beat %ld (%d/4)%s%s  %s", stream.name(), ms / 1000.0,
             static_cast<double>(stream.seconds()), (d.tempo - 1.0) * 100.0, static_cast<double>(grid.bpmAt(ms) * d.tempo),
             static_cast<long>(floor(b)) + 1, grid.inBarAt(static_cast<long>(floor(b))), d.keylock ? "  MT" : "",
             d.looping ? "  loop" : "", d.playing ? "playing" : "paused");
}
} // namespace engine

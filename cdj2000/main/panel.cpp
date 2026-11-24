// The front panel link (docs/panel.md): SCIF2 of the SH7763, clocked
// synchronous, 24-byte full-duplex exchanges driven by the main board.
#include "panel.h"
#include "tmu.h"

namespace cdj {

namespace {
constexpr uint32_t kBase = 0xFFE20000u;
inline volatile uint16_t &r16(uint32_t o) { return *reinterpret_cast<volatile uint16_t *>(kBase + o); }
inline volatile uint8_t &r8(uint32_t o) { return *reinterpret_cast<volatile uint8_t *>(kBase + o); }
constexpr uint32_t SCSMR = 0x00, SCBRR = 0x04, SCSCR = 0x08, SCFTDR = 0x0C, SCFSR = 0x10,
                   SCFRDR = 0x14, SCFCR = 0x18;
constexpr uint16_t RDF = 0x0002, TDFE = 0x0020, TEND = 0x0040;

uint8_t checksum(const uint8_t *p)
{
    unsigned s = 0;
    for (int i = 0; i < kPayload; i++) {
        s += p[i];
        if (s & 0x100) s = (s & 0xFF) + 1;     // end-around carry
    }
    return static_cast<uint8_t>(s);
}
} // namespace

void panel_init()
{
    r16(SCSCR) = 0;
    r16(SCSMR) = 0x80;          // clocked synchronous
    r8(SCBRR) = 12;
    r16(SCFCR) = 0x06;          // reset both FIFOs
    r16(SCFCR) = 0x30;
    r16(SCSCR) = 0x30;          // TE | RE
}

bool panel_exchange(const Frame &lamps, Frame &report)
{
    uint8_t tx[kFrame], rx[kFrame];
    for (int i = 0; i < kPayload; i++) tx[i] = lamps.b[i];
    tx[kPayload] = checksum(tx);
    tx[kPayload + 1] = kSync;
    int got = 0;
    const uint32_t t0 = tmu_us();
    // Each byte out clocks one in: write and drain as we go, so a short
    // receive FIFO on the real part never overflows.
    for (int i = 0; i < kFrame; i++) {
        while (!(r16(SCFSR) & TDFE)) { if (tmu_us() - t0 > 5000) return false; }
        r8(SCFTDR) = tx[i];
        r16(SCFSR) = static_cast<uint16_t>(r16(SCFSR) & ~(TDFE | TEND));
        while (got < kFrame && (r16(SCFSR) & RDF)) rx[got++] = r8(SCFRDR);
    }
    while (got < kFrame) {
        if (r16(SCFSR) & RDF) { rx[got++] = r8(SCFRDR); continue; }
        if (tmu_us() - t0 > 5000) return false;
    }
    r16(SCFSR) = static_cast<uint16_t>(r16(SCFSR) & ~RDF);
    if (rx[kPayload + 1] != kSync || rx[kPayload] != checksum(rx)) return false;
    for (int i = 0; i < kPayload; i++) report.b[i] = rx[i];
    return true;
}

namespace {
struct KeyMap { uint8_t off, mask; deck::Type type; int16_t delta; float position; };
constexpr KeyMap kKeys[] = {
    {0x10, 0x01, deck::Type::PlayPause, 0, 0},   {0x10, 0x02, deck::Type::Cue, 0, 0},
    {0x10, 0x04, deck::Type::ReloopExit, 0, 0},  {0x10, 0x08, deck::Type::LoopOut, 0, 0},
    {0x10, 0x10, deck::Type::LoopIn, 0, 0},      {0x10, 0x20, deck::Type::HotCue, 1, 0},
    {0x10, 0x40, deck::Type::HotCue, 2, 0},      {0x10, 0x80, deck::Type::HotCue, 3, 0},
    {0x11, 0x01, deck::Type::Load, 0, 0},        {0x11, 0x02, deck::Type::BeatLoop, 0, 4.0f},
    {0x12, 0x80, deck::Type::TempoRange, 0, 0},  {0x13, 0x40, deck::Type::KeyLock, 0, 0},
    {0x13, 0x80, deck::Type::TempoReset, 0, 0},  {0x14, 0x10, deck::Type::Back, 0, 0},
    {0x15, 0x01, deck::Type::CueCall, -1, 0},    {0x15, 0x02, deck::Type::CueCall, 1, 0},
    {0x15, 0x04, deck::Type::MemoryDelete, 0, 0}, {0x15, 0x08, deck::Type::MemoryAdd, 0, 0},
};
// Bytes whose bits are all keys (0x0F holds the lever and the jog's flags).
constexpr uint8_t kKeyBytes[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
inline uint16_t be16(const Frame &f, int o) { return static_cast<uint16_t>(f.b[o] << 8 | f.b[o + 1]); }
} // namespace

int PanelDecoder::feed(const Frame &r, Out *out, int max)
{
    int n = 0;
    auto push = [&](const Out &o) { if (n < max) out[n++] = o; };
    if (!have_last_) { last_ = r; have_last_ = true; tempo_ = -1.0f; }
    for (uint8_t off : kKeyBytes) {
        const uint8_t changed = static_cast<uint8_t>(r.b[off] ^ last_.b[off]);
        for (uint8_t m = 1; m; m = static_cast<uint8_t>(m << 1)) {
            if (!(changed & m)) continue;
            Out o;
            o.ev.pressed = (r.b[off] & m) != 0;
            o.raw_off = off;
            o.raw_mask = m;
            for (const KeyMap &k : kKeys) {
                if (k.off == off && k.mask == m) {
                    o.ev.type = k.type; o.ev.delta = k.delta; o.ev.position = k.position;
                }
            }
            push(o);
        }
    }
    if ((r.b[0x0F] ^ last_.b[0x0F]) & 0x02) {          // DIRECTION lever: set = FWD
        Out o; o.ev.type = deck::Type::Reverse; o.ev.pressed = !(r.b[0x0F] & 0x02); push(o);
    }
    if (const int16_t d = static_cast<int16_t>(be16(r, 0x08) - be16(last_, 0x08))) {
        Out o; o.ev.type = deck::Type::Jog; o.ev.delta = d; push(o);
    }
    if (const int8_t d = static_cast<int8_t>(r.b[0x0E] - last_.b[0x0E])) {
        Out o; o.ev.type = deck::Type::Browse; o.ev.delta = d; push(o);
    }
    const uint16_t pos = be16(r, 0x04), centre = BOARD_TEMPO_CENTRE_REF ? be16(r, 0x06) : 0x8000;
    if (centre) {                                       // no centre reference, no tempo
        float t = 0.5f + (static_cast<float>(pos) - static_cast<float>(centre)) / 65536.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        if (tempo_ < 0.0f || t - tempo_ > 0.001f || tempo_ - t > 0.001f) {
            if (tempo_ >= 0.0f) { Out o; o.ev.type = deck::Type::TempoFader; o.ev.position = t; push(o); }
            tempo_ = t;
        }
    }
    last_ = r;
    return n;
}

} // namespace cdj

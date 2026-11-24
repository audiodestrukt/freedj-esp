// freedj on the CDJ-2000's main board (SH7763, SH-4A).
// Milestone 2: the front panel drives freedj-esp's deck events, and the
// lamps answer (a stand-in transport until there is a player).
#include <stdint.h>
#include "scif.h"
#include "tmu.h"
#include "panel.h"

namespace {
constexpr int kCon = 0;         // the console SCIF (both get the banner)

void say(const char *s) { scif_puts(kCon, s); }

void print_event(const cdj::PanelDecoder::Out &o)
{
    say("panel: ");
    if (o.ev.type == deck::Type::None) {
        say("key "); scif_hex(kCon, o.raw_off); say("/"); scif_hex(kCon, o.raw_mask);
    } else {
        say(deck::typeName(o.ev.type));
    }
    switch (o.ev.type) {
    case deck::Type::Jog: case deck::Type::Browse:
        say(" "); scif_dec(kCon, o.ev.delta); break;
    case deck::Type::TempoFader:
        say(" "); scif_dec(kCon, static_cast<int32_t>(o.ev.position * 1000.0f)); say("/1000"); break;
    default:
        say(o.ev.pressed ? " down" : " up");
        if (o.ev.delta) { say(" "); scif_dec(kCon, o.ev.delta); }
    }
    say("\n");
}
} // namespace

extern "C" int main()
{
    for (int ch = 0; ch < 2; ch++) {
        scif_init(ch);
        scif_puts(ch, "\nfreedj cdj2000: main board up (SCIF");
        scif_putc(ch, static_cast<char>('0' + ch));
        scif_puts(ch, ")\n");
    }
    tmu_init();
    cdj::panel_init();

    cdj::Frame lamps, report;
    cdj::PanelDecoder decoder;
    cdj::PanelDecoder::Out ev[16];
    bool playing = false, master_tempo = false, tempo_reset = false, linked = false;
    uint32_t good = 0, bad = 0, next = tmu_us();
    for (;;) {
        cdj::set_lamp(lamps, cdj::kLampPlay, playing);
        cdj::set_lamp(lamps, cdj::kLampCue, !playing);
        cdj::set_lamp(lamps, cdj::kLampMasterTempo, master_tempo);
        cdj::set_lamp(lamps, cdj::kLampTempoReset, tempo_reset);
        if (cdj::panel_exchange(lamps, report)) {
            good++;
            if (!linked) { linked = true; say("panel: link up\n"); }
            const int n = decoder.feed(report, ev, 16);
            for (int i = 0; i < n; i++) {
                print_event(ev[i]);
                const deck::Event &e = ev[i].ev;
                if (e.type == deck::Type::PlayPause && e.pressed) playing = !playing;
                if (e.type == deck::Type::Cue && e.pressed) playing = false;
                if (e.type == deck::Type::KeyLock && e.pressed) master_tempo = !master_tempo;
                if (e.type == deck::Type::TempoReset && e.pressed) tempo_reset = !tempo_reset;
            }
        } else if (++bad == 1 || bad % 500 == 0) {
            say("panel: no answer ("); scif_dec(kCon, static_cast<int32_t>(bad)); say(" so far)\n");
        }
        next += 10000;                                  // an exchange every 10 ms
        while (static_cast<int32_t>(tmu_us() - next) < 0) {}
    }
}

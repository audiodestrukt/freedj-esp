// freedj on the CDJ-2000's main board (SH7763, SH-4A).
// Milestone 2: the front panel drives freedj-esp's deck events, and the
// lamps answer (a stand-in transport until there is a player).
// Milestone 3: Pro DJ Link, freedj-esp's own (p4/deck/main/prolink.cpp,
// compiled unchanged on shim/ and main/netstack.cpp), as a cooperative task.
#include <stdint.h>
#include "scif.h"
#include "tmu.h"
#include "panel.h"
#include "coop.h"
#include "con.h"
#include "net_hw.h"
#include "prolink.h"
#include "usb.h"
#include "fat.h"
#include "rekordbox.h"
#include "dsp.h"
#include "player.h"

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
    // ⚠ A made-up, locally administered address until the deck's own is
    // read from its flash: 169.254.32.1 on the wire.
    static const uint8_t mac[6] = {0x02, 0xcd, 0x20, 0x00, 0x20, 0x01};
    net_init(mac);
    prolink::begin(2, "CDJ-2000");
    // Milestone 5: the DSP, with our program on it, feeding the DAC.
    con_printf(dsp::boot() ? "dsp: running\n" : "dsp: did not start\n");
    // Milestone 4: the USB stick, its FAT, and freedj-esp's rekordbox scan of it.
    float track_bpm = 0;
    if (usb::open_stick(3000)) {
        if (!fat::mount()) {
            con_printf("usb: no FAT file system on the stick\n");
        } else {
            int n = 0;
            rekordbox::scan("/usb", [&](const char *audio, const char *anlz, float bpm) {
                con_printf("track %d: %s  (%.2f BPM; analysis %s)\n", ++n, audio, static_cast<double>(bpm), anlz);
                // Milestone 5: the first track that is a plain WAV goes in the player.
                if (!player::loaded() && player::load(audio)) track_bpm = bpm;
                return true;
            });
            con_printf("usb: %d analysed track%s on the stick\n", n, n == 1 ? "" : "s");
        }
    }

    cdj::Frame lamps, report;
    cdj::PanelDecoder decoder;
    cdj::PanelDecoder::Out ev[16];
    bool playing = false, master_tempo = false, tempo_reset = false, linked = false;
    uint32_t good = 0, bad = 0, next = tmu_us();
    int64_t next_summary = tmu_us64() + 5000000;
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
                if (e.type == deck::Type::PlayPause && e.pressed) {
                    playing = !playing;
                    player::play(playing);
                    con_printf("player: %s at %u ms\n", playing ? "play" : "pause", player::position_ms());
                }
                if (e.type == deck::Type::Cue && e.pressed) { playing = false; player::cue(); con_printf("player: cue\n"); }
                if (e.type == deck::Type::KeyLock && e.pressed) master_tempo = !master_tempo;
                if (e.type == deck::Type::TempoReset && e.pressed) tempo_reset = !tempo_reset;
            }
        } else if (++bad == 1 || bad % 500 == 0) {
            say("panel: no answer ("); scif_dec(kCon, static_cast<int32_t>(bad)); say(" so far)\n");
        }
        player::pump();
        if (playing && player::loaded() && !player::playing()) { playing = false; con_printf("player: end of track\n"); }
        prolink::Ours ours;
        ours.loaded = player::loaded();
        ours.playing = playing;
        ours.trackBpm = track_bpm;
        prolink::publish(ours);
        if (tmu_us64() >= next_summary) {
            next_summary += 5000000;
            char line[160];
            prolink::summary(line, sizeof line);
            con_printf("link: %s\n", line);
            con_printf("dsp: %u halves sent to the DAC, at %u ms\n", dsp::halves(), player::position_ms());
        }
        next += 10000;                                  // an exchange every 10 ms
        while (static_cast<int32_t>(tmu_us() - next) < 0) coop_yield();
    }
}

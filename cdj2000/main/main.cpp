// freedj on the CDJ-2000's main board (SH7763, SH-4A): the front panel
// (milestone 2), Pro DJ Link (3), the USB stick (4), audio through our DSP
// program (5), and the deck engine on them: freedj-esp's track stream
// (WAV, MP3), WSOLA and rekordbox grid under a CDJ transport (main/engine.cpp).
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
#include "engine.h"

namespace {
void print_event(const cdj::PanelDecoder::Out &o)
{
    if (o.ev.type == deck::Type::None) { con_printf("panel: key 0x%02x/0x%02x %s\n", o.raw_off, o.raw_mask, o.ev.pressed ? "down" : "up"); return; }
    switch (o.ev.type) {
    case deck::Type::Jog: case deck::Type::Browse:
        con_printf("panel: %s %d\n", deck::typeName(o.ev.type), o.ev.delta); break;
    case deck::Type::TempoFader:
        con_printf("panel: tempo %d/1000\n", static_cast<int>(o.ev.position * 1000.0f)); break;
    default:
        con_printf("panel: %s %s%s\n", deck::typeName(o.ev.type), o.ev.pressed ? "down" : "up", o.ev.delta ? (o.ev.delta > 0 ? " +" : " -") : "");
    }
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
    con_printf(dsp::boot() ? "dsp: running\n" : "dsp: did not start\n");
    engine::begin();
    if (usb::open_stick(3000)) {
        if (!fat::mount()) {
            con_printf("usb: no FAT file system on the stick\n");
        } else {
            int n = 0;
            rekordbox::scan("/usb", [&](const char *audio, const char *anlz, float bpm) {
                con_printf("track %d: %s  (%.2f BPM; analysis %s)\n", ++n, audio, static_cast<double>(bpm), anlz);
                engine::add_track(audio, anlz, bpm);   // the first one is loaded
                return true;
            });
            con_printf("usb: %d analysed track%s on the stick\n", n, n == 1 ? "" : "s");
        }
    }

    cdj::Frame lamps, report;
    cdj::PanelDecoder decoder;
    cdj::PanelDecoder::Out ev[16];
    bool linked = false;
    uint32_t bad = 0, next = tmu_us();
    int64_t next_summary = tmu_us64() + 5000000;
    for (;;) {
        const engine::Lamps l = engine::lamps();
        cdj::set_lamp(lamps, cdj::kLampPlay, l.play);
        cdj::set_lamp(lamps, cdj::kLampCue, l.cue);
        cdj::set_lamp(lamps, cdj::kLampLoopIn, l.loop_in);
        cdj::set_lamp(lamps, cdj::kLampLoopOut, l.loop_out);
        cdj::set_lamp(lamps, cdj::kLampReloop, l.reloop);
        cdj::set_lamp(lamps, cdj::kLampMasterTempo, l.master_tempo);
        cdj::set_lamp(lamps, cdj::kLampTempoReset, l.tempo_reset);
        if (cdj::panel_exchange(lamps, report)) {
            if (!linked) { linked = true; con_printf("panel: link up\n"); }
            const int n = decoder.feed(report, ev, 16);
            for (int i = 0; i < n; i++) {
                print_event(ev[i]);
                const deck::Event &e = ev[i].ev;
                const bool was = engine::playing();
                engine::apply(e);
                if ((e.type == deck::Type::PlayPause || e.type == deck::Type::Cue) && engine::playing() != was)
                    con_printf("deck: %s at %u ms\n", engine::playing() ? "play" : "pause", engine::position_ms());
            }
        } else if (++bad == 1 || bad % 500 == 0) {
            con_printf("panel: no answer (%u so far)\n", bad);
        }
        engine::service();
        if (tmu_us64() >= next_summary) {
            next_summary += 5000000;
            char line[200];
            prolink::summary(line, sizeof line);
            con_printf("link: %s\n", line);
            engine::summary(line, sizeof line);
            con_printf("deck: %s\n", line);
            con_printf("dsp: %u halves sent to the DAC, at %u ms\n", dsp::halves(), engine::position_ms());
        }
        next += 10000;                                  // a panel exchange every 10 ms
        while (static_cast<int32_t>(tmu_us() - next) < 0) coop_yield();
    }
}

#pragma once
// Pioneer Pro DJ Link: we announce as a player, send our status, and listen
// to every other player's beats and status to find the tempo master.
// Packet layouts are freedj-3000's (crates/link/src/prodj.rs), verified
// byte-for-byte against an XDJ-1000MK2 and prolink-cpp's virtual CDJ.
// Reference: https://djl-analysis.deepsymmetry.org/
//   50000  announce / keep-alive, broadcast every 1.5 s
//   50001  beat packets, broadcast once per beat by each playing player
//   50002  status packets, unicast to every known device ~5 times a second
// The task runs on core 0. Not called `link`: that's a POSIX function.
#include <stdint.h>

namespace prolink {

// The tempo master as last heard. All times are esp_timer microseconds.
struct Master {
    bool    present = false;   // a master is on the network (status or beats seen recently)
    uint8_t player = 0;
    float   bpm = 0;           // effective tempo: track BPM x pitch
    bool    beating = false;   // it sent a beat within the last two beat periods
    int64_t beatUs = 0;        // when its last beat packet arrived
    uint8_t beatInBar = 0;     // 1..4 for that beat
};

// What we tell the network about ourselves (from the audio task).
struct Ours {
    bool     loaded = false;
    bool     playing = false;
    bool     sync = false;
    float    trackBpm = 0;      // grid BPM at the playhead, unpitched; 0 = none
    float    pitch = 1.0f;      // speed multiplier
    uint32_t beat = 0;          // beat number from the track start (1-based), 0 = none
    uint8_t  beatInBar = 0;     // 1..4, 0 = none
};

void begin(uint8_t player, const char* name);
bool master(Master& out);       // false if no master; cheap, any task
void publish(const Ours& o);    // cheap, any task
// One line for the status report: address, peers, master.
void summary(char* out, int n);

} // namespace prolink

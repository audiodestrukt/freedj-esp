#include "prolink.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <initializer_list>
#include "esp_netif_ip_addr.h"
#include "lwip/sockets.h"
#include "net.h"

namespace prolink {

static const char* TAG = "link";

constexpr uint16_t kPortAnnounce = 50000, kPortBeat = 50001, kPortStatus = 50002;
constexpr uint8_t kAnnounce = 0x06, kBeat = 0x28, kStatus = 0x0a;
// Port 50001 control packets (Deep Symmetry's analysis; freedj-3000 for the
// handoff, tested against an XDJ-1000MK2).
constexpr uint8_t kFaderStart = 0x02, kOnAir = 0x03, kMasterReq = 0x26, kMasterRsp = 0x27, kSyncCtl = 0x2a;
constexpr uint32_t kSyncOn = 0x10, kSyncOff = 0x20, kBecomeMaster = 0x01;
static const uint8_t kMagic[10] = {'Q', 's', 'p', 't', '1', 'W', 'm', 'J', 'O', 'L'};
constexpr uint32_t kPitchUnity = 0x00100000;   // pitch field for +0 %
constexpr int64_t kForgetUs = 5000000;         // a peer silent this long is gone

// CDJ status packet captured from an XDJ-1000MK2 (firmware 1.44, playing a
// rekordbox track from USB, tempo master), name blanked. We copy it and
// overwrite the fields we understand, so every byte we don't understand is
// what Pioneer firmware sends. From freedj-3000 status_template.rs, which
// explains byte 0x1f (must be 0x01, or an XDJ aborts master handoffs).
static const uint8_t kStatusTemplate[0x124] = {
    0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6D, 0x4A, 0x4F, 0x4C, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x05, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x3F,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x3E,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x31, 0x2E, 0x34, 0x34,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xE4, 0xFF, 0xFA, 0x00, 0x10, 0x00, 0x00,
    0x80, 0x00, 0x31, 0x38, 0x7F, 0xFF, 0xFF, 0xFF, 0x00, 0x10, 0x00, 0x00, 0x00, 0x09, 0x01, 0xFF,
    0x00, 0x00, 0x00, 0x4E, 0x01, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x05, 0x75, 0x1F, 0x03, 0x00, 0x00,
    0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x07, 0x70, 0x00, 0x00, 0x07, 0x6D, 0x00, 0x1B, 0x24, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = v; }

// Pitch field to a speed multiplier; senders sometimes flag the top byte, so
// an absurd value falls back to unity rather than poisoning the tempo.
static float pitchOf(const uint8_t* p)
{
    const float m = (float)(be32(p) & 0x00FFFFFF) / kPitchUnity;
    return m >= 0.25f && m <= 4.0f ? m : 1.0f;
}

struct Peer {
    uint8_t  player = 0;
    uint32_t ip = 0;              // network byte order
    int64_t  seenUs = 0;          // last packet of any kind
    // From status packets.
    int64_t  statusUs = 0;
    bool     master = false, playing = false;
    uint8_t  handoffTo = 0xFF;    // Mh: the player it is handing master to
    uint32_t syncN = 0;
    float    trackBpm = 0, pitch = 1;
    // From beat packets.
    int64_t  beatUs = 0;
    float    beatBpm = 0;         // effective
    uint8_t  beatInBar = 0;
};
constexpr int kMaxPeers = 8;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Peer   s_peers[kMaxPeers];      // guarded by s_mux
static Ours   s_ours;                  // guarded by s_mux
static uint8_t s_player;
static char   s_name[20];
// Tempo master state, guarded by s_mux.
static bool     s_weMaster, s_wantMaster;
static int64_t  s_wantSince;
static uint8_t  s_handoffTo;           // granted to this player; drop master when it claims
static uint8_t  s_yieldedFrom;         // the master agreed to yield to us
static uint32_t s_ourSyncN, s_largestSyncN;
static int      s_remoteSync, s_faderStart;
static bool     s_onAir;
// A line to log after leaving the lock (logging inside it isn't allowed).
static char     s_note[96];

static void note(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_note, sizeof s_note, fmt, ap);
    va_end(ap);
}

static void logNote()
{
    char line[sizeof s_note];
    taskENTER_CRITICAL(&s_mux);
    strlcpy(line, s_note, sizeof line);
    s_note[0] = 0;
    taskEXIT_CRITICAL(&s_mux);
    if (line[0]) ESP_LOGI(TAG, "%s", line);
}

// Call inside s_mux.
static void takeMaster(const char* why)
{
    s_weMaster = true;
    s_wantMaster = false;
    s_yieldedFrom = 0;
    s_ourSyncN = s_largestSyncN + 1;
    note("we are tempo master (%s), sync counter %u", why, (unsigned)s_ourSyncN);
}

// Find or add a peer. Called inside s_mux, so it must not log: a new peer is
// reported through s_newPeer and logged by the caller after the lock.
static uint8_t s_newPeer;
static uint32_t s_newPeerIp;

static Peer* peerFor(uint8_t player, uint32_t ip, int64_t now)
{
    Peer* free = nullptr;
    for (auto& p : s_peers) {
        if (p.player == player && p.seenUs && now - p.seenUs < kForgetUs) { if (ip) p.ip = ip; p.seenUs = now; return &p; }
        if (!free && (!p.seenUs || now - p.seenUs >= kForgetUs)) free = &p;
    }
    if (!free) return nullptr;
    *free = Peer{};
    free->player = player;
    free->ip = ip;
    free->seenUs = now;
    s_newPeer = player;
    s_newPeerIp = ip;
    return free;
}

static void logNewPeer()
{
    uint8_t player;
    uint32_t ip;
    taskENTER_CRITICAL(&s_mux);
    player = s_newPeer; ip = s_newPeerIp; s_newPeer = 0;
    taskEXIT_CRITICAL(&s_mux);
    if (player) ESP_LOGI(TAG, "player %u at " IPSTR, player, IP2STR((esp_ip4_addr_t*)&ip));
}

static bool isLink(const uint8_t* d, int n) { return n > 0x0b && !memcmp(d, kMagic, 10); }

static void onAnnounce(const uint8_t* d, int n, int64_t now)
{
    if (!isLink(d, n) || d[0x0a] != kAnnounce || n < 0x30) return;
    const uint8_t player = d[0x24];
    if (player == s_player || player == 0) return;
    uint32_t ip;
    memcpy(&ip, d + 0x2c, 4);
    taskENTER_CRITICAL(&s_mux);
    peerFor(player, ip, now);
    taskEXIT_CRITICAL(&s_mux);
}

static void onBeat(const uint8_t* d, int n, uint32_t fromIp, int64_t now)
{
    if (!isLink(d, n) || d[0x0a] != kBeat || n < 0x60) return;
    const uint8_t player = d[0x21];
    if (player == s_player) return;
    const float bpm = be16(d + 0x5a) / 100.0f * pitchOf(d + 0x54);   // track BPM x pitch
    taskENTER_CRITICAL(&s_mux);
    if (Peer* p = peerFor(player, fromIp, now)) {
        p->beatUs = now;
        p->beatBpm = bpm;
        p->beatInBar = d[0x5c] >= 1 && d[0x5c] <= 4 ? d[0x5c] : 1;
    }
    taskEXIT_CRITICAL(&s_mux);
}

static void onStatus(const uint8_t* d, int n, uint32_t fromIp, int64_t now)
{
    if (!isLink(d, n) || d[0x0a] != kStatus || n < 0xcc) return;
    const uint8_t player = d[0x21];
    if (player == s_player) return;
    const uint8_t f = d[0x89];
    const uint16_t bpm = be16(d + 0x92);
    taskENTER_CRITICAL(&s_mux);
    if (Peer* p = peerFor(player, fromIp, now)) {
        p->statusUs = now;
        p->master = f & 0x20;
        p->playing = f & 0x40;
        p->trackBpm = bpm == 0xffff ? 0 : bpm / 100.0f;
        p->pitch = pitchOf(d + 0x8c);
        p->handoffTo = d[0x9f];
        p->syncN = be32(d + 0x84);
        if (p->syncN > s_largestSyncN) s_largestSyncN = p->syncN;
        // The master names us as its successor: take it. (Only after its
        // status says so; taking it on the yield reply made an XDJ abort.)
        if (p->master && p->handoffTo == s_player && !s_weMaster && (s_wantMaster || s_yieldedFrom == player))
            takeMaster("handed over");
        // The player we granted master to has claimed it: let go.
        if (p->master && s_weMaster && s_handoffTo == player) {
            s_weMaster = false;
            s_handoffTo = 0;
            note("handed tempo master to player %u", player);
        }
    }
    taskEXIT_CRITICAL(&s_mux);
}

// Port 50001, other than beats: master handoff, sync control, mixer.
static void onControl(const uint8_t* d, int n, uint32_t fromIp, int sock);

bool master(Master& out)
{
    const int64_t now = esp_timer_get_time();
    out = Master{};
    taskENTER_CRITICAL(&s_mux);
    if (s_weMaster) { taskEXIT_CRITICAL(&s_mux); return false; }   // we are the reference
    // The player whose status says master; failing that (no status reaching
    // us yet), the only player sending beats.
    const Peer* m = nullptr;
    int beaters = 0;
    const Peer* beater = nullptr;
    for (const auto& p : s_peers) {
        if (!p.seenUs || now - p.seenUs >= kForgetUs) continue;
        if (p.master && now - p.statusUs < 2000000) m = &p;
        if (p.beatUs && now - p.beatUs < 2000000) { beaters++; beater = &p; }
    }
    if (!m && beaters == 1) m = beater;
    if (m) {
        out.present = true;
        out.player = m->player;
        const bool fresh = m->beatUs && m->beatBpm > 0;
        // Status updates the tempo 5x a second, beats once per beat; prefer
        // whichever is newer.
        if (m->statusUs > m->beatUs && m->trackBpm > 0) out.bpm = m->trackBpm * m->pitch;
        else out.bpm = fresh ? m->beatBpm : m->trackBpm * m->pitch;
        out.beatUs = m->beatUs;
        out.beatInBar = m->beatInBar;
        out.beating = fresh && out.bpm > 0 && now - m->beatUs < (int64_t)(2 * 60e6 / out.bpm);
    }
    taskEXIT_CRITICAL(&s_mux);
    return out.present;
}

bool isMaster()
{
    taskENTER_CRITICAL(&s_mux);
    const bool m = s_weMaster;
    taskEXIT_CRITICAL(&s_mux);
    return m;
}

void requestMaster()
{
    taskENTER_CRITICAL(&s_mux);
    if (!s_weMaster) { s_wantMaster = true; s_wantSince = esp_timer_get_time(); s_yieldedFrom = 0; }
    taskEXIT_CRITICAL(&s_mux);
}

static int take(int& v)
{
    taskENTER_CRITICAL(&s_mux);
    const int r = v;
    v = 0;
    taskEXIT_CRITICAL(&s_mux);
    return r;
}
int takeRemoteSync() { return take(s_remoteSync); }
int takeFaderStart() { return take(s_faderStart); }
bool onAir() { return s_onAir; }

void publish(const Ours& o)
{
    taskENTER_CRITICAL(&s_mux);
    s_ours = o;
    taskEXIT_CRITICAL(&s_mux);
}

void summary(char* out, int n)
{
    const uint32_t ip = net::ip();
    Master m;
    master(m);
    int peers = 0;
    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_mux);
    for (const auto& p : s_peers) if (p.seenUs && now - p.seenUs < kForgetUs) peers++;
    taskEXIT_CRITICAL(&s_mux);
    int len = snprintf(out, n, "player %u  " IPSTR "%s  peers %d", s_player, IP2STR((esp_ip4_addr_t*)&ip),
                       net::linkUp() ? "" : " (no link)", peers);
    if (isMaster() && len < n)
        snprintf(out + len, n - len, "  WE ARE MASTER%s", s_onAir ? "  on air" : "");
    else if (m.present && len < n)
        snprintf(out + len, n - len, "  master %u @ %.2f BPM%s%s", m.player, m.bpm, m.beating ? "" : " (not playing)",
                 s_onAir ? "  on air" : "");
}

static void sendTo(int s, uint32_t ip, uint16_t port, const uint8_t* d, int n)
{
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    sendto(s, d, n, 0, (sockaddr*)&a, sizeof a);
}

// ── Packets we send ──────────────────────────────────────────────────────────

static int buildAnnounce(uint8_t* p, uint32_t ip, const uint8_t mac[6])
{
    memset(p, 0, 0x36);
    memcpy(p, kMagic, 10);
    p[0x0a] = kAnnounce;
    memcpy(p + 0x0c, s_name, 20);
    p[0x20] = 0x01;
    p[0x21] = 0x02;
    put16(p + 0x22, 0x36);
    p[0x24] = s_player;
    p[0x25] = 0x01;                 // device type: CDJ
    memcpy(p + 0x26, mac, 6);
    memcpy(p + 0x2c, &ip, 4);
    p[0x30] = 0x01;
    p[0x34] = 0x01;
    return 0x36;
}

static int buildStatus(uint8_t* p, const Ours& o, uint32_t counter)
{
    memcpy(p, kStatusTemplate, sizeof kStatusTemplate);
    memcpy(p + 0x0b, s_name, 20);
    p[0x1f] = 0x01;
    p[0x21] = s_player;
    p[0x24] = s_player;
    // Track source: our own player's USB, rekordbox-analysed, so peers trust
    // the BPM we report.
    p[0x28] = o.loaded ? s_player : 0;
    p[0x29] = o.loaded ? 0x03 : 0;
    p[0x2a] = o.loaded ? 0x01 : 0;
    p[0x7b] = !o.loaded ? 0x00 : o.emergencyLoop ? 0x12 : o.playing ? 0x03 : 0x05;
    memcpy(p + 0x7c, "0.1 ", 4);
    bool master;
    uint8_t handoff;
    uint32_t syncN;
    bool air;
    taskENTER_CRITICAL(&s_mux);
    master = s_weMaster; handoff = s_handoffTo; syncN = s_ourSyncN; air = s_onAir;
    taskEXIT_CRITICAL(&s_mux);
    p[0x89] = 0x84 | (o.playing ? 0x40 : 0) | (master ? 0x20 : 0) | (o.sync ? 0x10 : 0) | (air ? 0x08 : 0);
    p[0x8a] = o.playing ? 0xFF : 0x8D;
    p[0x8b] = o.playing ? 0xFA : 0xFE;
    const uint32_t pitch = (uint32_t)(o.pitch * kPitchUnity);
    for (int off : {0x8c, 0x98, 0xc0, 0xc4}) put32(p + off, pitch);
    const bool bpm = o.loaded && o.trackBpm > 0;
    put16(p + 0x90, bpm ? 0x8000 : 0x7FFF);
    put16(p + 0x92, bpm ? (uint16_t)(o.trackBpm * 100 + 0.5f) : 0xFFFF);
    p[0x9d] = o.playing ? 0x0D : 0x01;
    p[0x9e] = master ? 0x01 : 0x00;
    p[0x9f] = handoff ? handoff : 0xFF;
    put32(p + 0xa0, o.beat ? o.beat : 0xFFFFFFFF);
    put16(p + 0xa4, 0x01FF);
    p[0xa6] = o.beatInBar;
    put32(p + 0xc8, counter);
    put32(p + 0x84, syncN);
    return sizeof kStatusTemplate;
}

// Common header of the small port-50001 control packets.
static int controlHeader(uint8_t* p, uint8_t kind, uint16_t payload)
{
    memset(p, 0, 0x24 + payload);
    memcpy(p, kMagic, 10);
    p[0x0a] = kind;
    memcpy(p + 0x0b, s_name, 20);
    p[0x1f] = 0x01;
    p[0x21] = s_player;
    put16(p + 0x22, payload);
    return 0x24 + payload;
}

// Beat packet (0x28, 96 bytes), broadcast on each of our beats. BPM is the
// track's (unpitched), pitch separate: receivers multiply them (Beat Link's
// effective tempo). The six countdowns are ms to the next beat, the 2nd beat,
// the next bar, the 4th beat, the 2nd bar and the 8th beat.
static int buildBeat(uint8_t* p, const Ours& o)
{
    memset(p, 0, 0x60);
    memcpy(p, kMagic, 10);
    p[0x0a] = kBeat;
    memcpy(p + 0x0b, s_name, 20);
    p[0x1f] = 0x01;
    p[0x21] = s_player;
    put16(p + 0x22, 0x60 - 0x24);
    const float eff = o.trackBpm * o.pitch;
    const float beatMs = 60000.0f / (eff > 1 ? eff : 1);
    const int bib = o.nextBeatInBar >= 1 && o.nextBeatInBar <= 4 ? o.nextBeatInBar : 1;
    const float toBar = 5 - bib;
    const float counts[6] = {1, 2, toBar, 4, toBar + 4, 8};
    for (int i = 0; i < 6; i++) put32(p + 0x24 + 4 * i, (uint32_t)(counts[i] * beatMs + 0.5f));
    memset(p + 0x3c, 0xFF, 24);
    put32(p + 0x54, (uint32_t)(o.pitch * kPitchUnity));
    put16(p + 0x5a, (uint16_t)(o.trackBpm * 100 + 0.5f));
    p[0x5c] = (uint8_t)bib;
    p[0x5f] = s_player;
    return 0x60;
}

static void onControl(const uint8_t* d, int n, uint32_t fromIp, int sock)
{
    if (!isLink(d, n) || n < 0x28) return;
    static uint8_t out[0x2c];
    switch (d[0x0a]) {
    case kMasterReq: {                    // a player asks us for master
        const uint8_t from = d[0x27];
        bool yield = false;
        taskENTER_CRITICAL(&s_mux);
        if (s_weMaster) { yield = true; s_handoffTo = from; note("player %u asks for master: yielding", from); }
        taskEXIT_CRITICAL(&s_mux);
        if (yield) {
            const int len = controlHeader(out, kMasterRsp, 8);
            put32(out + 0x24, s_player);
            put32(out + 0x28, 1);
            sendTo(sock, fromIp, kPortBeat, out, len);
        }
        break;
    }
    case kMasterRsp:                      // the master answered our request
        if (n >= 0x2c) {
            taskENTER_CRITICAL(&s_mux);
            if (d[0x2b] == 1 && s_wantMaster) {
                s_yieldedFrom = d[0x27];
                note("player %u agreed to yield master; waiting for it to name us", d[0x27]);
            }
            taskEXIT_CRITICAL(&s_mux);
        }
        break;
    case kSyncCtl:                        // sync on/off, or become master
        // Sent straight to the target player. (freedj reads 0x27 as the
        // target, prolink-cpp writes the sender there; don't depend on it.)
        if (n >= 0x2c) {
            const uint32_t cmd = be32(d + 0x28);
            taskENTER_CRITICAL(&s_mux);
            if (cmd == kSyncOn) s_remoteSync = 1;
            else if (cmd == kSyncOff) s_remoteSync = 2;
            else if (cmd == kBecomeMaster && !s_weMaster) { s_wantMaster = true; s_wantSince = esp_timer_get_time(); }
            note("player %u sent sync control 0x%02x", d[0x21], (unsigned)cmd);
            taskEXIT_CRITICAL(&s_mux);
        }
        break;
    case kOnAir:                          // mixer: which channels are on air
        if (s_player >= 1 && s_player <= 4) {
            const bool air = d[0x24 + s_player - 1] != 0;
            taskENTER_CRITICAL(&s_mux);
            if (air != s_onAir) note("mixer: channel %u %s air", s_player, air ? "on" : "off");
            s_onAir = air;
            taskEXIT_CRITICAL(&s_mux);
        }
        break;
    case kFaderStart:                     // mixer: 0 start, 1 stop, 2 leave alone
        if (s_player >= 1 && s_player <= 4) {
            const uint8_t c = d[0x24 + s_player - 1];
            if (c == 0 || c == 1) {
                taskENTER_CRITICAL(&s_mux);
                s_faderStart = c == 0 ? 1 : 2;
                taskEXIT_CRITICAL(&s_mux);
            }
        }
        break;
    }
}

// ── Task ─────────────────────────────────────────────────────────────────────

static int udpSocket(uint16_t port)
{
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) ESP_LOGE(TAG, "bind %u failed", port);
    return s;
}

static void task(void*)
{
    const int sa = udpSocket(kPortAnnounce), sb = udpSocket(kPortBeat), ss = udpSocket(kPortStatus);
    static uint8_t buf[512], out[sizeof kStatusTemplate];
    int64_t nextAnnounce = 0, nextStatus = 0, lastMasterReq = 0;
    uint32_t counter = 0, lastIp = 0, lastBeatSent = 0;
    // The beat we're waiting to send. Held here, because the audio task's
    // "next beat" moves on as soon as the beat passes, and a wake-up a
    // moment late would otherwise skip it.
    Ours pend;
    bool pending = false;
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(sa, &rd); FD_SET(sb, &rd); FD_SET(ss, &rd);
        // Packets wake us at once; the timeout paces our sends, and wakes us
        // in time for our next beat.
        int64_t waitUs = 50000;
        if (pending) {
            const int64_t to = pend.nextBeatUs - esp_timer_get_time();
            if (to < waitUs) waitUs = to < 1000 ? 1000 : to;
        }
        timeval tv = {0, (long)waitUs};
        const int maxfd = sa > sb ? (sa > ss ? sa : ss) : (sb > ss ? sb : ss);
        if (select(maxfd + 1, &rd, nullptr, nullptr, &tv) > 0) {
            for (int s : {sb, ss, sa}) {      // beats first: their arrival time is the beat
                if (!FD_ISSET(s, &rd)) continue;
                sockaddr_in from;
                socklen_t fl = sizeof from;
                const int n = recvfrom(s, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                const int64_t now = esp_timer_get_time();
                if (n <= 0) continue;
                if (s == sb) {
                    if (n > 0x0a && buf[0x0a] == kBeat) onBeat(buf, n, from.sin_addr.s_addr, now);
                    else onControl(buf, n, from.sin_addr.s_addr, sb);
                }
                else if (s == ss) onStatus(buf, n, from.sin_addr.s_addr, now);
                else onAnnounce(buf, n, now);
                logNewPeer();
                logNote();
            }
        }

        const uint32_t ip = net::ip();
        if (ip != lastIp) {
            lastIp = ip;
            if (ip) ESP_LOGI(TAG, "on the network as " IPSTR ", player %u", IP2STR((esp_ip4_addr_t*)&ip), s_player);
        }
        if (!ip) continue;
        const int64_t now = esp_timer_get_time();
        const uint32_t bcast = ip | ~net::netmask();
        // Our beat: send it when it's heard.
        {
            taskENTER_CRITICAL(&s_mux);
            const Ours o = s_ours;
            taskEXIT_CRITICAL(&s_mux);
            if (!o.playing || o.trackBpm <= 0 || !o.nextBeatUs) {
                pending = false;
                lastBeatSent = 0;
            } else {
                // Moved backwards (loop wrap, cue, jump): start counting again.
                if (o.nextBeat < lastBeatSent || (pending && o.nextBeat < pend.nextBeat)) {
                    lastBeatSent = o.nextBeat - 1;
                    pending = false;
                }
                // The audio moved on past the beat we hold: send it now (late)
                // rather than lose it, then take the new one.
                if (pending && o.nextBeat > pend.nextBeat) {
                    sendTo(sb, bcast, kPortBeat, out, buildBeat(out, pend));
                    lastBeatSent = pend.nextBeat;
                    pending = false;
                }
                if (o.nextBeat > lastBeatSent) { pend = o; pending = true; }   // refresh the prediction
                if (pending && now >= pend.nextBeatUs - 500) {
                    sendTo(sb, bcast, kPortBeat, out, buildBeat(out, pend));
                    lastBeatSent = pend.nextBeat;
                    pending = false;
                }
            }
        }
        // Asking for master: take it if nobody has it, else ask the master.
        {
            uint32_t masterIp = 0;
            bool asking;
            taskENTER_CRITICAL(&s_mux);
            asking = s_wantMaster && !s_weMaster;
            if (asking) {
                for (const auto& p : s_peers)
                    if (p.seenUs && now - p.seenUs < kForgetUs && p.master && now - p.statusUs < 2000000) masterIp = p.ip;
                if (!masterIp) takeMaster("nobody had it");
                else if (now - s_wantSince > 5000000) { s_wantMaster = false; note("master handoff timed out"); }
            }
            const bool send = s_wantMaster && !s_weMaster && masterIp && !s_yieldedFrom && now - lastMasterReq >= 1000000;
            taskEXIT_CRITICAL(&s_mux);
            logNote();
            if (send) {
                const int len = controlHeader(out, kMasterReq, 4);
                put32(out + 0x24, s_player);
                sendTo(sb, masterIp, kPortBeat, out, len);
                lastMasterReq = now;
                ESP_LOGI(TAG, "asking the tempo master to hand over");
            }
        }
        if (now >= nextAnnounce) {
            nextAnnounce = now + 1500000;
            uint8_t mac[6];
            net::mac(mac);
            const int n = buildAnnounce(out, ip, mac);
            sendTo(sa, bcast, kPortAnnounce, out, n);
        }
        // Status 5x a second, and at once when our master flag changes: an
        // outgoing master waits to see the new one claim it.
        static bool sentMaster = false;
        const bool masterNow = isMaster();
        if (now >= nextStatus || masterNow != sentMaster) {
            sentMaster = masterNow;
            nextStatus = now + 200000;
            Ours o;
            uint32_t ips[kMaxPeers];
            int np = 0;
            taskENTER_CRITICAL(&s_mux);
            o = s_ours;
            for (const auto& p : s_peers)
                if (p.seenUs && now - p.seenUs < kForgetUs && p.ip) ips[np++] = p.ip;
            taskEXIT_CRITICAL(&s_mux);
            const int n = buildStatus(out, o, ++counter);
            for (int i = 0; i < np; i++) sendTo(ss, ips[i], kPortStatus, out, n);
        }
    }
}

void begin(uint8_t player, const char* name)
{
    s_player = player;
    memset(s_name, 0, sizeof s_name);
    memcpy(s_name, name, strnlen(name, sizeof s_name));   // NUL padded, not terminated: 20 bytes on the wire
    // Above the UI and loader so beat packets are timestamped promptly.
    xTaskCreatePinnedToCore(task, "link", 4096, nullptr, 7, nullptr, 0);
}

} // namespace prolink

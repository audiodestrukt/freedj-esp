#include "stream.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "minimp3_ex.h"

static const char* TAG = "stream";

static constexpr size_t kChunk = 2304;   // frames per refill (2 MP3 frames; 9 KB stereo)
static int16_t s_bounce[kChunk * 2];     // internal RAM: fast SD DMA

static uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t u16(const uint8_t* p) { return p[0] | p[1] << 8; }

// --- WAV: 16-bit PCM, read straight from the file ---------------------------
namespace {

struct WavSource : TrackStream::Source {
    FILE* f = nullptr;
    long  dataOffset = 0;
    ~WavSource() override { if (f) fclose(f); }

    bool open(const char* path)
    {
        f = fopen(path, "rb");
        if (!f) return false;
        uint8_t hdr[12];
        if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
            ESP_LOGE(TAG, "not a WAV file"); return false;
        }
        uint16_t format = 0, bits = 0;
        uint32_t dataBytes = 0;
        bool haveFmt = false, haveData = false;
        while (!haveData) {
            uint8_t ch[8];
            if (fread(ch, 1, 8, f) != 8) break;
            const uint32_t len = u32(ch + 4);
            if (!memcmp(ch, "fmt ", 4)) {
                uint8_t fmt[40] = {};
                const size_t n = len < sizeof fmt ? len : sizeof fmt;
                if (fread(fmt, 1, n, f) != n) break;
                if (len > n) fseek(f, len - n, SEEK_CUR);
                format = u16(fmt); channels = u16(fmt + 2); rate = u32(fmt + 4); bits = u16(fmt + 14);
                if (format == 0xFFFE && len >= 26) format = u16(fmt + 24);  // WAVE_FORMAT_EXTENSIBLE
                haveFmt = true;
            } else if (!memcmp(ch, "data", 4)) {
                dataBytes = len;
                haveData = true;
            } else {
                fseek(f, len + (len & 1), SEEK_CUR);
            }
        }
        if (!haveFmt || !haveData || format != 1 || bits != 16 || channels < 1 || channels > 2) {
            ESP_LOGE(TAG, "need 16-bit PCM mono/stereo (fmt=%u bits=%u ch=%u)", format, bits, channels);
            return false;
        }
        dataOffset = ftell(f);
        frames = dataBytes / (channels * 2);
        return true;
    }
    bool seek(size_t frame) override { return fseek(f, dataOffset + (long)frame * channels * 2, SEEK_SET) == 0; }
    size_t read(int16_t* out, size_t n) override { return fread(out, channels * 2, n, f); }
};

// --- MP3: minimp3, sample-accurate seek via the frame index -----------------
struct Mp3Source : TrackStream::Source {
    FILE* f = nullptr;
    mp3dec_io_t io = {};
    mp3dec_ex_t* dec = nullptr;   // ~16 KB: heap, not stack
    ~Mp3Source() override
    {
        if (dec) { mp3dec_ex_close(dec); heap_caps_free(dec); }
        if (f) fclose(f);
    }

    static size_t ioRead(void* buf, size_t size, void* user) { return fread(buf, 1, size, (FILE*)user); }
    static int ioSeek(uint64_t pos, void* user) { return fseek((FILE*)user, (long)pos, SEEK_SET); }

    bool open(const char* path)
    {
        f = fopen(path, "rb");
        if (!f) return false;
        // Big stdio buffer: the open-time index scan reads the whole file.
        setvbuf(f, nullptr, _IOFBF, 32 * 1024);
        io.read = ioRead; io.read_data = f;
        io.seek = ioSeek; io.seek_data = f;
        dec = (mp3dec_ex_t*)heap_caps_calloc(1, sizeof(mp3dec_ex_t), MALLOC_CAP_8BIT);
        if (!dec) return false;
        const int64_t t0 = esp_timer_get_time();
        // SEEK_TO_SAMPLE scans the file once to build a frame index: exact
        // length, and later seeks land on the exact sample (no resync click).
        if (mp3dec_ex_open_cb(dec, &io, MP3D_SEEK_TO_SAMPLE) != 0 || !dec->samples) {
            ESP_LOGE(TAG, "not a decodable MP3");
            return false;
        }
        channels = dec->info.channels;
        rate = dec->info.hz;
        frames = dec->samples / channels;
        ESP_LOGI(TAG, "indexed in %.2f s, %d kbps", (esp_timer_get_time() - t0) / 1e6, dec->info.bitrate_kbps);
        return channels == 1 || channels == 2;
    }
    bool seek(size_t frame) override { return mp3dec_ex_seek(dec, (uint64_t)frame * channels) == 0; }
    size_t read(int16_t* out, size_t n) override { return mp3dec_ex_read(dec, out, n * channels) / channels; }
};

} // namespace

// --- window ------------------------------------------------------------------

bool TrackStream::begin(size_t windowBytes)
{
    cap_ = windowBytes / 4;
    ring_ = (int16_t*)heap_caps_malloc(cap_ * 4, MALLOC_CAP_SPIRAM);
    if (!ring_) { ESP_LOGE(TAG, "can't allocate %u KB window in PSRAM", (unsigned)(windowBytes / 1024)); return false; }
    ESP_LOGI(TAG, "window %.1f MB = %.0f s at 44.1 kHz", windowBytes / 1048576.0, cap_ / 44100.0);
    return true;
}

void TrackStream::close()
{
    open_ = false;
    delete src_;
    src_ = nullptr;
    winStart_ = winEnd_ = playhead_ = 0;
    frames_ = 0;
}

bool TrackStream::open(const char* path)
{
    close();
    if (!ring_) return false;
    const char* dot = strrchr(path, '.');
    Source* src = nullptr;
    if (dot && !strcasecmp(dot, ".mp3")) {
        auto* m = new Mp3Source;
        if (m->open(path)) src = m; else delete m;
    } else {
        auto* w = new WavSource;
        if (w->open(path)) src = w; else delete w;
    }
    if (!src) { ESP_LOGE(TAG, "can't play %s", path); return false; }

    src_ = src;
    rate_ = src->rate;
    frames_ = src->frames;
    srcPos_ = 0;
    const char* base = strrchr(path, '/');
    snprintf(name_, sizeof name_, "%s", base ? base + 1 : path);
    winStart_ = winEnd_ = playhead_ = 0;
    open_ = true;
    ESP_LOGI(TAG, "%s: %u Hz %s, %.1f s", name_, (unsigned)rate_, src->channels == 2 ? "stereo" : "mono", seconds());
    return true;
}

bool TrackStream::service()
{
    if (!open_) return false;
    const size_t behind = cap_ * 35 / 100;   // keep ~25 s back for cue / jog
    const size_t ph = playhead_;

    // Playhead left the window (seek / cue far away): restart it there.
    if (ph < winStart_ || ph > winEnd_ + kChunk) {
        const size_t start = ph > 2048 ? ph - 2048 : 0;
        winEnd_ = start;     // publish "empty" before moving the start
        winStart_ = start;
    }

    size_t end = ph + (cap_ - behind);
    if (end > frames_) end = frames_;
    const size_t we = winEnd_;
    if (we >= end) return false;
    size_t n = end - we;
    if (n > kChunk) n = kChunk;

    // Free ring space by dropping the oldest frames first, so a reader never
    // accepts a slot that's about to be overwritten.
    if (we + n - winStart_ > cap_) winStart_ = we + n - cap_;

    if (srcPos_ != we) {
        if (!src_->seek(we)) { ESP_LOGE(TAG, "seek to %u failed", (unsigned)we); return false; }
        srcPos_ = we;
    }
    const size_t got = src_->read(s_bounce, n);
    srcPos_ += got;
    if (got == 0) { frames_ = we; return false; }  // shorter than the header said

    const uint16_t ch = src_->channels;
    for (size_t i = 0; i < got; i++) {
        int16_t* dst = ring_ + ((we + i) % cap_) * 2;
        if (ch == 2) { dst[0] = s_bounce[2 * i]; dst[1] = s_bounce[2 * i + 1]; }
        else         { dst[0] = dst[1] = s_bounce[i]; }
    }
    winEnd_ = we + got;
    return true;
}

bool TrackStream::get(size_t frame, int16_t* lr, size_t n) const
{
    if (frame < winStart_ || frame + n > winEnd_) return false;
    for (size_t i = 0; i < n; i++) {
        const int16_t* src = ring_ + ((frame + i) % cap_) * 2;
        lr[2 * i] = src[0];
        lr[2 * i + 1] = src[1];
    }
    return true;
}

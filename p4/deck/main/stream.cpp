#include "stream.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char* TAG = "stream";

static uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t u16(const uint8_t* p) { return p[0] | p[1] << 8; }

static constexpr size_t kChunk = 4096;   // frames per SD read (16 KB stereo)
static int16_t s_bounce[kChunk * 2];     // internal RAM: fast SD DMA

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
    if (f_) fclose(f_);
    f_ = nullptr;
    winStart_ = winEnd_ = playhead_ = 0;
    frames_ = 0;
}

bool TrackStream::open(const char* path)
{
    close();
    if (!ring_) return false;
    FILE* f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "can't open %s", path); return false; }
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        ESP_LOGE(TAG, "%s: not a WAV file", path); fclose(f); return false;
    }
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0, dataBytes = 0;
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
        ESP_LOGE(TAG, "%s: need 16-bit PCM mono/stereo (fmt=%u bits=%u ch=%u)", path, format, bits, channels);
        fclose(f); return false;
    }
    f_ = f;
    dataOffset_ = ftell(f);
    channels_ = channels;
    rate_ = rate;
    frames_ = dataBytes / (channels * 2);
    filePos_ = 0;
    const char* base = strrchr(path, '/');
    snprintf(name_, sizeof name_, "%s", base ? base + 1 : path);
    winStart_ = winEnd_ = playhead_ = 0;
    open_ = true;
    ESP_LOGI(TAG, "%s: %u Hz %s, %.1f s", name_, (unsigned)rate, channels == 2 ? "stereo" : "mono", seconds());
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

    if (filePos_ != we) {
        fseek(f_, dataOffset_ + (long)we * channels_ * 2, SEEK_SET);
        filePos_ = we;
    }
    const size_t got = fread(s_bounce, channels_ * 2, n, f_);
    filePos_ += got;
    if (got == 0) { frames_ = we; return false; }  // short file: trust what's there

    for (size_t i = 0; i < got; i++) {
        int16_t* dst = ring_ + ((we + i) % cap_) * 2;
        if (channels_ == 2) { dst[0] = s_bounce[2 * i]; dst[1] = s_bounce[2 * i + 1]; }
        else                { dst[0] = dst[1] = s_bounce[i]; }
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

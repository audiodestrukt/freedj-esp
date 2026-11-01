// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rekordbox.h"

namespace library {

static const char* TAG = "library";

Snapshot::~Snapshot()
{
    heap_caps_free(e_);
    heap_caps_free(pool_);
}

int Snapshot::find(const char* path) const
{
    for (size_t i = 0; i < n_; i++) if (!strcmp(pool_ + e_[i].path, path)) return (int)i;
    return -1;
}

// Collects entries with their strings in growable PSRAM buffers.
class Builder {
public:
    ~Builder() { heap_caps_free(e_); heap_caps_free(pool_); }
    bool add(const char* path, const char* anlz, const char* name, float bpm)
    {
        Snapshot::Entry e;
        e.path = put(path); e.anlz = put(anlz); e.name = put(name); e.bpm = bpm;
        if (!pool_) return false;
        if (n_ == cap_) {
            cap_ = cap_ ? cap_ * 2 : 64;
            auto* p = (Snapshot::Entry*)heap_caps_realloc(e_, cap_ * sizeof *e_, MALLOC_CAP_SPIRAM);
            if (!p) return false;
            e_ = p;
        }
        e_[n_++] = e;
        return true;
    }
    size_t size() const { return n_; }
    // Sort the entries added since `from` by path.
    void sortFrom(size_t from)
    {
        std::sort(e_ + from, e_ + n_, [this](const Snapshot::Entry& a, const Snapshot::Entry& b) {
            return strcmp(pool_ + a.path, pool_ + b.path) < 0;
        });
    }
    void append(const Snapshot& s)
    {
        for (int i = 0; i < s.size(); i++) add(s.path(i), s.anlz(i), s.name(i), s.bpm(i));
    }
    Ptr finish()
    {
        auto* s = new Snapshot();
        s->e_ = e_; s->n_ = n_; s->pool_ = pool_;
        e_ = nullptr; pool_ = nullptr; n_ = cap_ = len_ = poolCap_ = 0;
        return Ptr(s);
    }

private:
    uint32_t put(const char* str)
    {
        const size_t n = strlen(str) + 1;
        if (len_ + n > poolCap_) {
            poolCap_ = std::max(poolCap_ * 2, len_ + n + 4096);
            char* p = (char*)heap_caps_realloc(pool_, poolCap_, MALLOC_CAP_SPIRAM);
            if (!p) { heap_caps_free(pool_); pool_ = nullptr; return 0; }
            pool_ = p;
        }
        memcpy(pool_ + len_, str, n);
        const uint32_t at = len_;
        len_ += n;
        return at;
    }
    Snapshot::Entry* e_ = nullptr;
    size_t n_ = 0, cap_ = 0;
    char* pool_ = nullptr;
    size_t len_ = 0, poolCap_ = 0;
};

static std::mutex s_mu;                 // guards s_cur and s_sd
static Ptr s_cur, s_sd;                 // current list; SD-only list
static std::atomic<uint32_t> s_gen{0};
static std::atomic<bool> s_scanning{false}, s_cancel{false};
static std::atomic<int> s_found{0};
static SemaphoreHandle_t s_scanDone;    // given when the scan task exits
static char s_usbRoot[16];

static void publish(Ptr p)
{
    {
        std::lock_guard<std::mutex> l(s_mu);
        s_cur = std::move(p);
    }
    s_gen++;
}

Ptr get()
{
    std::lock_guard<std::mutex> l(s_mu);
    return s_cur;
}

uint32_t generation() { return s_gen.load(); }
Progress progress() { return {s_scanning.load(), s_found.load()}; }

static bool playable(const char* name)
{
    const char* dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".wav") || !strcasecmp(dot, ".mp3"));
}

static void walk(const std::string& dir, int depth, int maxDepth, std::vector<std::string>& out)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string path = dir + "/" + e->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < maxDepth) walk(path, depth + 1, maxDepth, out); }
        else if (playable(e->d_name)) out.push_back(path);
    }
    closedir(d);
}

void begin(const char* sdRoot)
{
    s_scanDone = xSemaphoreCreateBinary();
    std::vector<std::string> sd;
    if (sdRoot) walk(sdRoot, 0, 4, sd);
    std::sort(sd.begin(), sd.end());
    Builder b;
    for (auto& p : sd) {
        const size_t slash = p.find('/', 1);   // strip the mount ("/sdcard/")
        b.add(p.c_str(), "", slash == std::string::npos ? p.c_str() : p.c_str() + slash + 1, 0);
    }
    Ptr snap = b.finish();
    {
        std::lock_guard<std::mutex> l(s_mu);
        s_sd = snap;
    }
    publish(snap);
    ESP_LOGI(TAG, "SD: %d playable track(s)", snap->size());
}

// Reads every rekordbox ANLZ file on the stick, at low priority on core 0,
// while the screen shows progress. On a full-speed hub port, 218 tracks take
// about 13 s.
static void scanTask(void*)
{
    const int64_t t0 = esp_timer_get_time();
    Builder b;
    int skipped = 0;
    rekordbox::scan(s_usbRoot, [&](const char* audio, const char* anlz, float bpm) {
        if (s_cancel) return false;
        struct stat st;
        if (!playable(audio) || stat(audio, &st) != 0) { skipped++; return true; }
        const char* slash = strrchr(audio, '/');
        char name[160];
        snprintf(name, sizeof name, "%6.2f  %s", bpm, slash ? slash + 1 : audio);
        b.add(audio, anlz, name, bpm);
        s_found = (int)b.size();
        return true;
    });
    if (!s_cancel) {
        b.sortFrom(0);
        const int usb = (int)b.size();
        Ptr sd;
        {
            std::lock_guard<std::mutex> l(s_mu);
            sd = s_sd;
        }
        if (sd) b.append(*sd);
        publish(b.finish());
        ESP_LOGI(TAG, "USB: %d rekordbox track(s) in %.1f s, %d skipped (not WAV/MP3, or file missing)",
                 usb, (esp_timer_get_time() - t0) / 1e6, skipped);
    }
    s_scanning = false;
    xSemaphoreGive(s_scanDone);
    vTaskDelete(nullptr);
}

void scanUsb(const char* usbRoot)
{
    if (s_scanning) return;
    strlcpy(s_usbRoot, usbRoot, sizeof s_usbRoot);
    s_cancel = false;
    s_found = 0;
    s_scanning = true;
    xSemaphoreTake(s_scanDone, 0);
    xTaskCreatePinnedToCore(scanTask, "libscan", 6144, nullptr, 2, nullptr, 0);
}

void dropUsb()
{
    if (s_scanning) {
        s_cancel = true;
        xSemaphoreTake(s_scanDone, pdMS_TO_TICKS(5000));
    }
    Ptr sd;
    {
        std::lock_guard<std::mutex> l(s_mu);
        sd = s_sd;
    }
    publish(sd);
    ESP_LOGI(TAG, "USB gone: %d SD track(s)", sd ? sd->size() : 0);
}

} // namespace library

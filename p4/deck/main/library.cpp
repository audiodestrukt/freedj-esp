#include "library.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <algorithm>
#include "esp_log.h"
#include "esp_timer.h"
#include "rekordbox.h"

namespace library {

static const char* TAG = "library";
static std::vector<std::string> s_tracks, s_anlz;
static std::vector<float> s_bpm;
static std::vector<bool> s_rb;
static const std::string s_none;

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

void scan(const char* sdRoot, const char* usbRoot)
{
    s_tracks.clear(); s_anlz.clear(); s_bpm.clear(); s_rb.clear();
    if (usbRoot) {
        const int64_t t0 = esp_timer_get_time();
        int skipped = 0;
        for (auto& t : rekordbox::scan(usbRoot)) {
            struct stat st;
            if (!playable(t.audio.c_str()) || stat(t.audio.c_str(), &st) != 0) { skipped++; continue; }
            s_tracks.push_back(t.audio);
            s_anlz.push_back(t.anlz);
            s_bpm.push_back(t.bpm);
            s_rb.push_back(true);
        }
        ESP_LOGI(TAG, "USB: %u rekordbox track(s) in %.1f s, %d skipped (not WAV/MP3, or file missing)",
                 (unsigned)s_tracks.size(), (esp_timer_get_time() - t0) / 1e6, skipped);
    }
    std::vector<std::string> sd;
    if (sdRoot) walk(sdRoot, 0, 4, sd);
    std::sort(sd.begin(), sd.end());
    for (auto& p : sd) { s_tracks.push_back(p); s_anlz.push_back(""); s_bpm.push_back(0); s_rb.push_back(false); }
    ESP_LOGI(TAG, "%u playable track(s)", (unsigned)s_tracks.size());
}

const std::vector<std::string>& tracks() { return s_tracks; }
const std::string& anlz(int i) { return i >= 0 && i < (int)s_anlz.size() ? s_anlz[i] : s_none; }
float bpm(int i) { return i >= 0 && i < (int)s_bpm.size() ? s_bpm[i] : 0; }

std::string displayName(int i)
{
    if (i < 0 || i >= (int)s_tracks.size()) return "";
    const std::string& p = s_tracks[i];
    if (s_rb[i]) {
        const size_t slash = p.rfind('/');
        char b[16];
        snprintf(b, sizeof b, "%6.2f  ", s_bpm[i]);
        return b + p.substr(slash + 1);
    }
    // Strip the mount ("/sdcard/").
    const size_t slash = p.find('/', 1);
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

} // namespace library

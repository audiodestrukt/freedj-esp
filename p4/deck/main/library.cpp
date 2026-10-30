#include "library.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <algorithm>
#include "esp_log.h"

namespace library {

static const char* TAG = "library";
static std::vector<std::string> s_tracks;

static bool playable(const char* name)
{
    const char* dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, ".wav");  // MP3 comes later
}

static void walk(const std::string& dir, int depth, int maxDepth)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string path = dir + "/" + e->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < maxDepth) walk(path, depth + 1, maxDepth); }
        else if (playable(e->d_name)) s_tracks.push_back(path);
    }
    closedir(d);
}

void scan(const char* root, int maxDepth)
{
    s_tracks.clear();
    walk(root, 0, maxDepth);
    std::sort(s_tracks.begin(), s_tracks.end());
    ESP_LOGI(TAG, "%u playable track(s)", (unsigned)s_tracks.size());
}

const std::vector<std::string>& tracks() { return s_tracks; }

} // namespace library

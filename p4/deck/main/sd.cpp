// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sd.h"
#include "board.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

namespace sd {

static const char* TAG = "sd";

bool begin()
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40 MHz

    // The slot's IO is powered by the P4's on-chip LDO channel 4.
    sd_pwr_ctrl_ldo_config_t ldo = {};
    ldo.ldo_chan_id = 4;
    sd_pwr_ctrl_handle_t pwr = nullptr;
    if (sd_pwr_ctrl_new_on_chip_ldo(&ldo, &pwr) != ESP_OK) {
        ESP_LOGE(TAG, "LDO power for the SD slot failed");
        return false;
    }
    host.pwr_ctrl_handle = pwr;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
#ifdef SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = board::SD_CLK; slot.cmd = board::SD_CMD;
    slot.d0 = board::SD_D0; slot.d1 = board::SD_D1; slot.d2 = board::SD_D2; slot.d3 = board::SD_D3;
#endif
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mc = {};
    mc.format_if_mount_failed = false;  // never wipe someone's music
    mc.max_files = 8;
    mc.allocation_unit_size = 16 * 1024;

    sdmmc_card_t* card = nullptr;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(kMount, &host, &slot, &mc, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return false;
    }
    sdmmc_card_print_info(stdout, card);
    return true;
}

static bool isAudio(const char* name)
{
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    static const char* kExt[] = {".mp3", ".wav", ".flac", ".aif", ".aiff", ".m4a", ".ogg"};
    for (const char* e : kExt) if (!strcasecmp(dot, e)) return true;
    return false;
}

static void walk(const char* dir, int depth, int maxDepth, int& files, long long& bytes)
{
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* ent;
    char path[300];
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < maxDepth) walk(path, depth + 1, maxDepth, files, bytes);
            else ESP_LOGI(TAG, "  %s/ (not descended)", path + strlen(kMount));
        } else if (isAudio(ent->d_name)) {
            ESP_LOGI(TAG, "  %7.1f MB  %s", st.st_size / 1048576.0, path + strlen(kMount));
            files++;
            bytes += st.st_size;
        }
    }
    closedir(d);
}

void listAudio(int maxDepth)
{
    int files = 0;
    long long bytes = 0;
    ESP_LOGI(TAG, "audio files on the card:");
    walk(kMount, 0, maxDepth, files, bytes);
    ESP_LOGI(TAG, "%d audio file(s), %.1f MB", files, bytes / 1048576.0);
}

} // namespace sd

#include "usb_stick.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"

namespace usb_stick {

static const char* TAG = "stick";
static QueueHandle_t s_q;                 // msc_host_event_t from the driver callback
static EventGroupHandle_t s_flags;
constexpr EventBits_t kMounted = 1;
static msc_host_device_handle_t s_dev;
static msc_host_vfs_handle_t s_vfs;

static void onEvent(const msc_host_event_t* e, void*) { xQueueSend(s_q, e, 0); }

// Driver calls (install/register) must not run inside the driver's callback.
static void task(void*)
{
    for (;;) {
        msc_host_event_t e;
        xQueueReceive(s_q, &e, portMAX_DELAY);
        if (e.event == msc_host_event_t::MSC_DEVICE_CONNECTED && !s_dev) {
            if (msc_host_install_device(e.device.address, &s_dev) != ESP_OK) {
                ESP_LOGE(TAG, "install failed");
                s_dev = nullptr;
                continue;
            }
            msc_host_device_info_t info;
            if (msc_host_get_device_info(s_dev, &info) == ESP_OK)
                ESP_LOGI(TAG, "stick %04X:%04X, %u MB", info.idVendor, info.idProduct,
                         (unsigned)((uint64_t)info.sector_count * info.sector_size >> 20));
            esp_vfs_fat_mount_config_t mc = {};
            mc.format_if_mount_failed = false;   // never touch the user's stick
            mc.max_files = 4;
            mc.allocation_unit_size = 0;
            if (msc_host_vfs_register(s_dev, kMount, &mc, &s_vfs) != ESP_OK) {
                ESP_LOGE(TAG, "mount failed (FAT32 only, no exFAT)");
                msc_host_uninstall_device(s_dev);
                s_dev = nullptr;
                continue;
            }
            ESP_LOGI(TAG, "mounted at %s", kMount);
            xEventGroupSetBits(s_flags, kMounted);
        } else if (e.event == msc_host_event_t::MSC_DEVICE_DISCONNECTED && e.device.handle == s_dev) {
            ESP_LOGW(TAG, "stick removed");
            xEventGroupClearBits(s_flags, kMounted);
            msc_host_vfs_unregister(s_vfs);
            msc_host_uninstall_device(s_dev);
            s_dev = nullptr;
        }
    }
}

void begin()
{
    s_q = xQueueCreate(4, sizeof(msc_host_event_t));
    s_flags = xEventGroupCreate();
    msc_host_driver_config_t cfg = {};
    cfg.create_backround_task = true;
    cfg.task_priority = 5;
    cfg.stack_size = 4096;
    cfg.core_id = 0;
    cfg.callback = onEvent;
    ESP_ERROR_CHECK(msc_host_install(&cfg));
    xTaskCreatePinnedToCore(task, "stick", 4096, nullptr, 4, nullptr, 0);
}

bool waitMounted(int ms)
{
    return xEventGroupWaitBits(s_flags, kMounted, pdFALSE, pdTRUE, pdMS_TO_TICKS(ms)) & kMounted;
}

bool mounted() { return xEventGroupGetBits(s_flags) & kMounted; }

} // namespace usb_stick

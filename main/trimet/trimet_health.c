#include "trimet_health.h"

#include <inttypes.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#include "esp_core_dump.h"
#endif

#include "indicator_storage.h"

#include "trimet_client.h"

#define STORAGE_KEY      "trimet-health"
#define CHECK_PERIOD_US  (60 * 1000 * 1000)
#define HEAP_LOG_EVERY   10           /* checks, i.e. minutes */
#define RTC_MAGIC        0x54524D48   /* "TRMH" */

static const char *TAG = "trimet-health";

/* Survives software resets, panics and watchdog resets (not power loss), so
 * the next boot knows how long this one ran and whether we restarted it. */
typedef struct {
    uint32_t magic;
    uint32_t uptime_s;
    uint32_t stale_restart;
} rtc_state_t;

static RTC_NOINIT_ATTR rtc_state_t s_rtc;

/* Saved in NVS. */
typedef struct {
    uint32_t count;
    int32_t  reason;
    uint32_t stale;
    uint32_t uptime_s;
    int64_t  time;
} crash_record_t;

static SemaphoreHandle_t  s_mutex;
static esp_reset_reason_t s_boot_reason;
static crash_record_t     s_crash;      /* guarded by s_mutex */
static bool               s_need_time;  /* this boot was a crash; fill in s_crash.time once the clock is set */
static bool               s_coredump;
static esp_timer_handle_t s_timer;

static bool is_unexpected(esp_reset_reason_t r, bool stale)
{
    switch (r) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
        return true;
    case ESP_RST_SW:
        return stale;
    default:
        return false;
    }
}

const char *trimet_health_reason_name(esp_reset_reason_t reason, bool stale)
{
    switch (reason) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_SW:       return stale ? "no data" : "restart";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "power dip";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "unknown";
    }
}

static void save_crash(void)
{
    crash_record_t copy;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    copy = s_crash;
    xSemaphoreGive(s_mutex);
    if (indicator_storage_write(STORAGE_KEY, &copy, sizeof(copy)) != ESP_OK) {
        ESP_LOGW(TAG, "failed to save restart record");
    }
}

static void log_heap(uint32_t uptime_s)
{
    ESP_LOGI(TAG, "up %" PRIu32 " min; internal free %u (min %u, largest %u); PSRAM free %u (min %u)",
             uptime_s / 60,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

/* Restart if there has been no successful fetch for CONFIG_TRIMET_STALE_RESTART_MIN.
 * Recovers from Wi-Fi or network stack states that never come back on their own. */
static void check_stale(int64_t now_ms)
{
#if CONFIG_TRIMET_STALE_RESTART_MIN > 0
    trimet_client_status_t st;
    const int64_t limit_ms = (int64_t)CONFIG_TRIMET_STALE_RESTART_MIN * 60 * 1000;

    trimet_client_get(NULL, &st);
    /* A restart cannot fix a missing or rejected appID. */
    if (st.state == TRIMET_STATE_NO_APP_ID ||
        (st.state == TRIMET_STATE_HTTP_ERROR && st.http_status == 403)) {
        return;
    }
    /* last_ok_uptime_ms is 0 before the first success, so this also covers
     * never getting data after boot. */
    if (now_ms - st.last_ok_uptime_ms > limit_ms) {
        ESP_LOGE(TAG, "no arrivals for %d min (%s); restarting",
                 CONFIG_TRIMET_STALE_RESTART_MIN, st.error);
        s_rtc.stale_restart = 1;
        esp_restart();
    }
#endif
}

static void timer_cb(void *arg)
{
    static uint32_t ticks;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const uint32_t uptime_s = now_ms / 1000;

    s_rtc.uptime_s = uptime_s;

    if (s_need_time && time(NULL) > 1704067200) {   /* clock set (after 2024-01-01) */
        s_need_time = false;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_crash.time = time(NULL) - uptime_s;
        xSemaphoreGive(s_mutex);
        save_crash();
    }

    if (++ticks % HEAP_LOG_EVERY == 0) {
        log_heap(uptime_s);
    }
    check_stale(now_ms);
}

esp_err_t trimet_health_init(void)
{
    const bool rtc_valid = s_rtc.magic == RTC_MAGIC;
    const uint32_t prev_uptime_s = rtc_valid ? s_rtc.uptime_s : 0;
    const bool stale = rtc_valid && s_rtc.stale_restart;
    crash_record_t rec;
    size_t len = sizeof(rec);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_rtc.magic = RTC_MAGIC;
    s_rtc.uptime_s = 0;
    s_rtc.stale_restart = 0;

    s_boot_reason = esp_reset_reason();
    if (indicator_storage_read(STORAGE_KEY, &rec, &len) == ESP_OK && len == sizeof(rec)) {
        s_crash = rec;
    }

    if (is_unexpected(s_boot_reason, stale)) {
        s_crash.count++;
        s_crash.reason = s_boot_reason;
        s_crash.stale = stale;
        s_crash.uptime_s = prev_uptime_s;
        s_crash.time = 0;
        s_need_time = true;
        save_crash();
        ESP_LOGW(TAG, "unexpected restart #%" PRIu32 ": %s after %" PRIu32 " min",
                 s_crash.count, trimet_health_reason_name(s_boot_reason, stale), prev_uptime_s / 60);
    } else {
        ESP_LOGI(TAG, "boot reason: %s", trimet_health_reason_name(s_boot_reason, stale));
    }

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    {
        size_t addr, size;
        s_coredump = esp_core_dump_image_get(&addr, &size) == ESP_OK;
        if (s_coredump) {
            ESP_LOGW(TAG, "core dump saved in flash (%u bytes); read it with 'idf.py coredump-info'", size);
        }
    }
#endif

    const esp_timer_create_args_t args = {
        .callback = timer_cb,
        .name = "trimet-health",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, CHECK_PERIOD_US));
    return ESP_OK;
}

void trimet_health_get(trimet_health_t *out)
{
    memset(out, 0, sizeof(*out));
    out->uptime_s = esp_timer_get_time() / 1000000;
    out->boot_reason = s_boot_reason;
    out->coredump = s_coredump;
    if (!s_mutex) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    out->crash_count = s_crash.count;
    out->crash_reason = s_crash.reason;
    out->crash_stale = s_crash.stale;
    out->crash_uptime_s = s_crash.uptime_s;
    out->crash_time = s_crash.time;
    xSemaphoreGive(s_mutex);
}

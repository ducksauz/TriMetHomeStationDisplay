#include "trimet_dimmer.h"

#include <stddef.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "config.h"
#include "indicator_storage.h"
#include "view_data.h"

#include "trimet_format.h"

#define STORAGE_KEY     "trimet-dim"
#define CHECK_PERIOD_US (15 * 1000 * 1000)

static const char *TAG = "trimet-dim";

static const trimet_dim_cfg_t DEFAULT_CFG = {
    .enabled = true,
    .start_min = 22 * 60,   /* 10 PM */
    .end_min = 6 * 60,      /* 6 AM */
    .brightness = TRIMET_DIM_NIGHT_DEFAULT,
};

typedef enum { DIM_UNKNOWN, DIM_OFF, DIM_ON } dim_state_t;

static SemaphoreHandle_t  s_mutex;
static trimet_dim_cfg_t   s_cfg;
static dim_state_t        s_state = DIM_UNKNOWN;
static esp_timer_handle_t s_timer;

static void set_brightness(int brightness)
{
    /* Handled by the template's display model (indicator_display.c). Not
     * saved to NVS, so the stored day brightness is untouched. */
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS_UPDATE,
                      &brightness, sizeof(brightness), pdMS_TO_TICKS(100));
}

/* Decide whether to dim now and change the backlight if needed. Runs on the
 * esp_timer task and, after a settings change, on the LVGL task. force sets
 * the backlight even if the state did not change (new level, end of preview). */
static void evaluate(bool force)
{
    time_t now = time(NULL);
    struct tm tm;
    bool dim;

    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 < 2024) {
        return;   /* clock not set yet; leave the backlight alone */
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    dim = s_cfg.enabled && trimet_time_in_window(tm.tm_hour * 60 + tm.tm_min, s_cfg.start_min, s_cfg.end_min);

    if (dim && (s_state != DIM_ON || force)) {
        ESP_LOGI(TAG, "dimming to %d%%", s_cfg.brightness);
        set_brightness(s_cfg.brightness);
        s_state = DIM_ON;
    } else if (!dim && (s_state == DIM_ON || force)) {
        ESP_LOGI(TAG, "restoring brightness to %d%%", TRIMET_DIM_DAY_BRIGHTNESS);
        set_brightness(TRIMET_DIM_DAY_BRIGHTNESS);
        s_state = DIM_OFF;
    } else if (!dim) {
        s_state = DIM_OFF;   /* first check during the day: nothing to change */
    }
    xSemaphoreGive(s_mutex);
}

static void timer_cb(void *arg)
{
    evaluate(false);
}

static bool cfg_valid(const trimet_dim_cfg_t *cfg)
{
    return cfg->start_min < 24 * 60 && cfg->end_min < 24 * 60 &&
           cfg->brightness >= TRIMET_DIM_NIGHT_MIN && cfg->brightness <= TRIMET_DIM_NIGHT_MAX;
}

esp_err_t trimet_dimmer_init(void)
{
    trimet_dim_cfg_t cfg;
    size_t len = sizeof(cfg);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    if (indicator_storage_read(STORAGE_KEY, &cfg, &len) == ESP_OK) {
        if (len == offsetof(trimet_dim_cfg_t, brightness)) {
            cfg.brightness = TRIMET_DIM_NIGHT_DEFAULT;   /* saved before brightness was adjustable */
            len = sizeof(cfg);
        }
    }
    if (len == sizeof(cfg) && cfg_valid(&cfg)) {
        s_cfg = cfg;
    } else {
        s_cfg = DEFAULT_CFG;
    }
    ESP_LOGI(TAG, "night dimming %s, %02d:%02d-%02d:%02d, %d%%", s_cfg.enabled ? "on" : "off",
             s_cfg.start_min / 60, s_cfg.start_min % 60, s_cfg.end_min / 60, s_cfg.end_min % 60,
             s_cfg.brightness);

    const esp_timer_create_args_t args = {
        .callback = timer_cb,
        .name = "trimet-dim",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, CHECK_PERIOD_US));
    return ESP_OK;
}

void trimet_dimmer_get(trimet_dim_cfg_t *cfg)
{
    if (!s_mutex) {
        *cfg = DEFAULT_CFG;
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *cfg = s_cfg;
    xSemaphoreGive(s_mutex);
}

void trimet_dimmer_set(const trimet_dim_cfg_t *cfg)
{
    trimet_dim_cfg_t copy = *cfg;

    if (!s_mutex || !cfg_valid(cfg)) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_cfg = copy;
    xSemaphoreGive(s_mutex);

    if (indicator_storage_write(STORAGE_KEY, &copy, sizeof(copy)) != ESP_OK) {
        ESP_LOGW(TAG, "failed to save settings");
    }
    evaluate(true);
}

void trimet_dimmer_preview(int brightness)
{
    set_brightness(brightness);
}

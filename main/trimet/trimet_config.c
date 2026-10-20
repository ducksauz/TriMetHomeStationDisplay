#include "trimet_config.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "indicator_storage.h"

#include "trimet_client.h"

#define STORAGE_KEY "trimet-cfg"

static const char *TAG = "trimet-cfg";

/* Saved in NVS. */
typedef struct {
    char    app_id[TRIMET_APP_ID_LEN];
    int16_t poll_sec;
    int16_t rotate_sec;
} config_t;

static SemaphoreHandle_t s_mutex;
static config_t          s_cfg = {
    .app_id = "",
    .poll_sec = CONFIG_TRIMET_POLL_INTERVAL_SEC,
    .rotate_sec = CONFIG_TRIMET_PAGE_ROTATE_SEC,
};

static int clamp_step(int v, int min, int max, int step)
{
    v = min + (v - min + step / 2) / step * step;
    return v < min ? min : v > max ? max : v;
}

static void lock(void)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

static void save(void)
{
    config_t copy;

    lock();
    copy = s_cfg;
    unlock();
    if (indicator_storage_write(STORAGE_KEY, &copy, sizeof(copy)) != ESP_OK) {
        ESP_LOGW(TAG, "failed to save settings");
    }
}

esp_err_t trimet_config_init(void)
{
    config_t cfg;
    size_t len = sizeof(cfg);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    if (indicator_storage_read(STORAGE_KEY, &cfg, &len) == ESP_OK && len == sizeof(cfg)) {
        cfg.app_id[sizeof(cfg.app_id) - 1] = '\0';
        cfg.poll_sec = clamp_step(cfg.poll_sec, TRIMET_POLL_MIN_SEC, TRIMET_POLL_MAX_SEC, TRIMET_POLL_STEP_SEC);
        cfg.rotate_sec = clamp_step(cfg.rotate_sec, TRIMET_ROTATE_MIN_SEC, TRIMET_ROTATE_MAX_SEC, TRIMET_ROTATE_STEP_SEC);
        lock();
        s_cfg = cfg;
        unlock();
    }
    /* Never log the appID itself. */
    ESP_LOGI(TAG, "appID %s, poll every %d s, page every %d s",
             s_cfg.app_id[0] ? "set" : "not set", s_cfg.poll_sec, s_cfg.rotate_sec);
    return ESP_OK;
}

void trimet_config_get_app_id(char *buf, size_t len)
{
    lock();
    strlcpy(buf, s_cfg.app_id, len);
    unlock();
}

int trimet_config_get_poll_sec(void)
{
    int v;

    lock();
    v = s_cfg.poll_sec;
    unlock();
    return v;
}

int trimet_config_get_rotate_sec(void)
{
    int v;

    lock();
    v = s_cfg.rotate_sec;
    unlock();
    return v;
}

void trimet_config_set_app_id(const char *app_id)
{
    lock();
    strlcpy(s_cfg.app_id, app_id, sizeof(s_cfg.app_id));
    unlock();
    save();
    trimet_client_refresh();   /* try the new appID now */
}

void trimet_config_set_poll_sec(int sec)
{
    lock();
    s_cfg.poll_sec = clamp_step(sec, TRIMET_POLL_MIN_SEC, TRIMET_POLL_MAX_SEC, TRIMET_POLL_STEP_SEC);
    unlock();
    save();
    trimet_client_refresh();   /* fetch now and restart the wait with the new interval */
}

void trimet_config_set_rotate_sec(int sec)
{
    lock();
    s_cfg.rotate_sec = clamp_step(sec, TRIMET_ROTATE_MIN_SEC, TRIMET_ROTATE_MAX_SEC, TRIMET_ROTATE_STEP_SEC);
    unlock();
    save();
}

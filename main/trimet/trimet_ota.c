#include "trimet_ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "trimet_client.h"
#include "trimet_format.h"

#define VERSION_FILE        "version.txt"
#define IMAGE_FILE          "trimet_station_display.bin"
#define HTTP_TIMEOUT_MS     15000
#define HTTP_RX_BUF         4096    /* GitHub's response headers are long */
#define HTTP_TX_BUF         2048    /* redirects go to long signed URLs */
#define TASK_STACK          8192
#define TASK_PRIO           3
#define PROBATION_MIN       5       /* minutes a new image must run before it is marked good */
#define PROBATION_CHECK_US  (30 * 1000 * 1000)

static const char *TAG = "trimet-ota";

static SemaphoreHandle_t   s_mutex;
static trimet_ota_status_t s_status;   /* guarded by s_mutex */
static volatile bool       s_busy;     /* a check or install task is running */
static esp_timer_handle_t  s_probation_timer;

/* ---- status ---- */

static void set_state(trimet_ota_state_t state, const char *error)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = state;
    strlcpy(s_status.error, error ? error : "", sizeof(s_status.error));
    xSemaphoreGive(s_mutex);
}

void trimet_ota_get(trimet_ota_status_t *out)
{
    if (!s_mutex) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_mutex);
}

const char *trimet_ota_running_version(void)
{
    return esp_app_get_description()->version;
}

/* For sizing: the internal RAM low point (since boot) and this task's unused stack. */
static void log_memory(const char *what)
{
    ESP_LOGI(TAG, "%s: internal min free %u; stack %u of %u bytes unused", what,
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), TASK_STACK);
}

/* ---- check ---- */

typedef struct {
    char buf[TRIMET_OTA_VERSION_LEN];
    int  len;
} version_body_t;

static esp_err_t version_event_cb(esp_http_client_event_t *evt)
{
    version_body_t *body = evt->user_data;

    /* Redirect responses have bodies too; keep only the final one. */
    if (evt->event_id == HTTP_EVENT_ON_DATA && esp_http_client_get_status_code(evt->client) == 200) {
        int n = evt->data_len;
        if (n > (int)sizeof(body->buf) - 1 - body->len) {
            n = sizeof(body->buf) - 1 - body->len;
        }
        memcpy(body->buf + body->len, evt->data, n);
        body->len += n;
        body->buf[body->len] = '\0';
    }
    return ESP_OK;
}

static void check_task(void *arg)
{
    version_body_t body = {0};
    esp_http_client_config_t cfg = {
        .url = CONFIG_TRIMET_OTA_URL "/" VERSION_FILE,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = HTTP_RX_BUF,
        .buffer_size_tx = HTTP_TX_BUF,
        .event_handler = version_event_cb,
        .user_data = &body,
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    int status = 0;
    char msg[64];

    trimet_client_pause(true);
    client = esp_http_client_init(&cfg);
    err = client ? esp_http_client_perform(client) : ESP_ERR_NO_MEM;
    if (client) {
        status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
    }
    trimet_client_pause(false);

    body.buf[strcspn(body.buf, " \r\n")] = '\0';
    if (err != ESP_OK) {
        snprintf(msg, sizeof(msg), "Check failed: %s", esp_err_to_name(err));
        set_state(TRIMET_OTA_ERROR, msg);
    } else if (status != 200) {
        snprintf(msg, sizeof(msg), "Check failed: HTTP %d", status);
        set_state(TRIMET_OTA_ERROR, msg);
    } else if (!trimet_version_valid(body.buf)) {
        set_state(TRIMET_OTA_ERROR, "Check failed: bad version file");
    } else {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        strlcpy(s_status.latest, body.buf, sizeof(s_status.latest));
        xSemaphoreGive(s_mutex);
        set_state(trimet_version_is_newer(body.buf, trimet_ota_running_version()) ? TRIMET_OTA_AVAILABLE
                                                                                   : TRIMET_OTA_UP_TO_DATE, NULL);
    }
    ESP_LOGI(TAG, "check: running %s, latest %s (%s)", trimet_ota_running_version(),
             body.buf[0] ? body.buf : "?", err == ESP_OK ? "ok" : esp_err_to_name(err));
    log_memory("check");
    s_busy = false;
    vTaskDeleteWithCaps(NULL);
}

/* ---- install ---- */

static esp_err_t check_image(esp_https_ota_handle_t ota, char *msg, size_t len)
{
    esp_app_desc_t desc;
    const esp_app_desc_t *running = esp_app_get_description();
    esp_err_t err = esp_https_ota_get_img_desc(ota, &desc);

    if (err != ESP_OK) {
        snprintf(msg, len, "Bad image: %s", esp_err_to_name(err));
        return err;
    }
    if (strncmp(desc.project_name, running->project_name, sizeof(desc.project_name)) != 0) {
        snprintf(msg, len, "Wrong firmware: %.32s", desc.project_name);
        return ESP_ERR_INVALID_VERSION;
    }
    ESP_LOGI(TAG, "installing %s over %s", desc.version, running->version);
    return ESP_OK;
}

static void install_task(void *arg)
{
    esp_http_client_config_t http = {
        .url = CONFIG_TRIMET_OTA_URL "/" IMAGE_FILE,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = HTTP_RX_BUF,
        .buffer_size_tx = HTTP_TX_BUF,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = {
        .http_config = &http,
    };
    esp_https_ota_handle_t ota = NULL;
    esp_err_t err;
    char msg[64] = "";

    trimet_client_pause(true);

    err = esp_https_ota_begin(&cfg, &ota);
    if (err != ESP_OK) {
        snprintf(msg, sizeof(msg), "Download failed: %s", esp_err_to_name(err));
        goto fail;
    }
    err = check_image(ota, msg, sizeof(msg));
    if (err != ESP_OK) {
        esp_https_ota_abort(ota);
        goto fail;
    }

    {
        const int total = esp_https_ota_get_image_size(ota);
        while ((err = esp_https_ota_perform(ota)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            if (total > 0) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_status.progress = (int)((int64_t)esp_https_ota_get_image_len_read(ota) * 100 / total);
                xSemaphoreGive(s_mutex);
            }
        }
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(ota)) {
        snprintf(msg, sizeof(msg), "Download failed: %s",
                 err != ESP_OK ? esp_err_to_name(err) : "incomplete");
        esp_https_ota_abort(ota);
        goto fail;
    }
    /* Verifies the image and switches the boot slot. */
    err = esp_https_ota_finish(ota);
    if (err != ESP_OK) {
        snprintf(msg, sizeof(msg), "Install failed: %s", esp_err_to_name(err));
        goto fail;
    }

    ESP_LOGI(TAG, "update installed; restarting");
    log_memory("install");
    set_state(TRIMET_OTA_RESTARTING, NULL);
    vTaskDelay(pdMS_TO_TICKS(1500));   /* let the screen show it */
    esp_restart();

fail:
    ESP_LOGE(TAG, "%s", msg);
    log_memory("install");
    set_state(TRIMET_OTA_ERROR, msg);
    trimet_client_pause(false);
    s_busy = false;
    vTaskDeleteWithCaps(NULL);
}

/* ---- start ---- */

/* The check task's stack is in PSRAM to save internal RAM. The install task
 * writes flash, which disables the cache and with it PSRAM, so its stack
 * must be internal. */
static void start(TaskFunction_t fn, const char *name, trimet_ota_state_t state, UBaseType_t stack_caps)
{
    if (!s_mutex || s_busy) {
        return;
    }
    s_busy = true;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.progress = 0;
    xSemaphoreGive(s_mutex);
    set_state(state, NULL);
    if (xTaskCreateWithCaps(fn, name, TASK_STACK, NULL, TASK_PRIO, NULL, stack_caps) != pdPASS) {
        set_state(TRIMET_OTA_ERROR, "Out of memory");
        s_busy = false;
    }
}

void trimet_ota_check(void)
{
    start(check_task, "ota-check", TRIMET_OTA_CHECKING, MALLOC_CAP_SPIRAM);
}

void trimet_ota_install(void)
{
    trimet_ota_status_t st;

    trimet_ota_get(&st);
    if (st.state == TRIMET_OTA_AVAILABLE) {
        start(install_task, "ota-install", TRIMET_OTA_DOWNLOADING, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
}

/* ---- probation ---- */

/* A new image is good once it has run PROBATION_MIN minutes and fetched
 * arrivals (or cannot, because there is no appID yet). Until then a restart,
 * whether from a crash, a watchdog or the no-data restart, boots the
 * previous image instead. */
static void probation_cb(void *arg)
{
    trimet_client_status_t st;

    if (esp_timer_get_time() < (int64_t)PROBATION_MIN * 60 * 1000000) {
        return;
    }
    trimet_client_get(NULL, &st);
    if (st.last_ok_uptime_ms == 0 && st.state != TRIMET_STATE_NO_APP_ID) {
        return;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "%s marked good", trimet_ota_running_version());
        esp_timer_stop(s_probation_timer);
    }
}

bool trimet_ota_rolled_back(char *buf, size_t len)
{
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    esp_app_desc_t desc;

    if (!bad || esp_ota_get_partition_description(bad, &desc) != ESP_OK) {
        return false;
    }
    strlcpy(buf, desc.version, len);
    return true;
}

esp_err_t trimet_ota_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    char bad[TRIMET_OTA_VERSION_LEN];

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "running %s from %s", trimet_ota_running_version(), running ? running->label : "?");
    if (trimet_ota_rolled_back(bad, sizeof(bad))) {
        ESP_LOGW(TAG, "update to %s failed; went back to %s", bad, trimet_ota_running_version());
    }

    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_timer_create_args_t args = {
            .callback = probation_cb,
            .name = "ota-probation",
        };
        ESP_LOGI(TAG, "new firmware on probation for %d min", PROBATION_MIN);
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_probation_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_probation_timer, PROBATION_CHECK_US));
    }
    return ESP_OK;
}

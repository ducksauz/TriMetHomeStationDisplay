#include "trimet_client.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "trimet_config.h"

#define TRIMET_URL_BASE      "https://developer.trimet.org/ws/v2/arrivals"
#define TRIMET_LOOKAHEAD_MIN 60        /* "minutes" param; API max is 60 */
#define HTTP_TIMEOUT_MS      10000
#define RESP_CHUNK           4096
#define RESP_MAX             (64 * 1024)
#define RETRY_MIN_MS         5000      /* first retry after an error, doubles up to the poll interval */
#define TASK_STACK           8192
#define TASK_PRIO            4
#define LOG_MAX_ARRIVALS     12

static const char *TAG = "trimet";

static SemaphoreHandle_t        s_mutex;
static TaskHandle_t             s_task;
static trimet_arrivals_t       *s_latest;   /* last good result, guarded by s_mutex */
static trimet_arrivals_t       *s_work;     /* poll task scratch */
static trimet_client_status_t   s_status;   /* guarded by s_mutex */
static bool                     s_have_data;
static volatile bool            s_have_ip;  /* station has an IP address */

static const char *state_name(trimet_state_t s)
{
    switch (s) {
    case TRIMET_STATE_IDLE:        return "idle";
    case TRIMET_STATE_OK:          return "ok";
    case TRIMET_STATE_NET_ERROR:   return "network error";
    case TRIMET_STATE_HTTP_ERROR:  return "HTTP error";
    case TRIMET_STATE_API_ERROR:   return "API error";
    case TRIMET_STATE_PARSE_ERROR: return "parse error";
    case TRIMET_STATE_NO_APP_ID:   return "no appID";
    }
    return "?";
}

static const char *arrival_status_name(trimet_status_t s)
{
    switch (s) {
    case TRIMET_STATUS_ESTIMATED: return "estimated";
    case TRIMET_STATUS_SCHEDULED: return "scheduled";
    case TRIMET_STATUS_DELAYED:   return "delayed";
    case TRIMET_STATUS_CANCELED:  return "canceled";
    default:                      return "unknown";
    }
}

/* Read the whole response body into a PSRAM buffer. Caller frees. */
static char *read_body(esp_http_client_handle_t client, int *out_len)
{
    size_t cap = RESP_CHUNK, len = 0;
    char *buf = heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buf) {
        return NULL;
    }
    for (;;) {
        int r;
        if (len == cap) {
            char *nbuf;
            if (cap >= RESP_MAX) {
                ESP_LOGE(TAG, "response larger than %d bytes", RESP_MAX);
                free(buf);
                return NULL;
            }
            cap *= 2;
            nbuf = heap_caps_realloc(buf, cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nbuf) {
                free(buf);
                return NULL;
            }
            buf = nbuf;
        }
        r = esp_http_client_read(client, buf + len, cap - len);
        if (r < 0) {
            free(buf);
            return NULL;
        }
        if (r == 0) {
            break;
        }
        len += r;
    }
    buf[len] = '\0';
    *out_len = (int)len;
    return buf;
}

static trimet_state_t fetch_once(const char *app_id, trimet_arrivals_t *out, int *http_status,
                                 char *err, size_t err_len)
{
    char url[256];
    esp_http_client_handle_t client;
    esp_err_t e;
    char *body = NULL;
    int body_len = 0;
    trimet_state_t state;

    *http_status = 0;
    err[0] = '\0';

    /* Never log this URL: it contains the appID. */
    snprintf(url, sizeof(url), TRIMET_URL_BASE "?locIDs=%s,%s&appID=%s&minutes=%d",
             CONFIG_TRIMET_STREETCAR_STOP_IDS, CONFIG_TRIMET_MAX_STOP_IDS,
             app_id, TRIMET_LOOKAHEAD_MIN);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };
    client = esp_http_client_init(&cfg);
    if (!client) {
        snprintf(err, err_len, "http client init failed");
        return TRIMET_STATE_NET_ERROR;
    }

    e = esp_http_client_open(client, 0);
    if (e != ESP_OK) {
        snprintf(err, err_len, "connect: %s", esp_err_to_name(e));
        state = TRIMET_STATE_NET_ERROR;
        goto done;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        snprintf(err, err_len, "no response headers");
        state = TRIMET_STATE_NET_ERROR;
        goto done;
    }
    *http_status = esp_http_client_get_status_code(client);

    body = read_body(client, &body_len);
    if (!body) {
        snprintf(err, err_len, "failed reading response");
        state = TRIMET_STATE_NET_ERROR;
        goto done;
    }

    if (*http_status != 200) {
        /* e.g. 403 "A valid appID is required to access this resource." */
        snprintf(err, err_len, "HTTP %d: %.60s", *http_status, body);
        state = TRIMET_STATE_HTTP_ERROR;
        goto done;
    }

    switch (trimet_parse_arrivals(body, body_len, out, err, err_len)) {
    case TRIMET_PARSE_OK:
        state = TRIMET_STATE_OK;
        break;
    case TRIMET_PARSE_API_ERROR:
        state = TRIMET_STATE_API_ERROR;
        break;
    default:
        snprintf(err, err_len, "malformed JSON (%d bytes)", body_len);
        state = TRIMET_STATE_PARSE_ERROR;
        break;
    }

done:
    free(body);
    esp_http_client_cleanup(client);
    return state;
}

static void log_arrivals(const trimet_arrivals_t *a)
{
    ESP_LOGI(TAG, "%d arrivals at %d stops", a->arrival_count, a->location_count);
    for (int i = 0; i < a->arrival_count && i < LOG_MAX_ARRIVALS; i++) {
        const trimet_arrival_t *x = &a->arrivals[i];
        /* Relative to server time, so this is right even before SNTP sync. */
        int64_t mins = (x->time_ms - a->query_time_ms) / 60000;
        ESP_LOGI(TAG, "  [%5" PRIu32 "] %3u %-44.44s %4" PRId64 " min  %s",
                 x->loc_id, x->route, x->short_sign, mins, arrival_status_name(x->status));
    }
}

static void poll_task(void *arg)
{
    uint32_t retry_ms = RETRY_MIN_MS;
    TickType_t wait = portMAX_DELAY;   /* until the station gets an IP */

    for (;;) {
        trimet_state_t state;
        int http_status;
        char err[TRIMET_ERROR_LEN];
        char app_id[TRIMET_APP_ID_LEN];
        uint32_t poll_ms;

        ulTaskNotifyTake(pdTRUE, wait);
        if (!s_have_ip) {
            wait = portMAX_DELAY;
            continue;
        }

        /* Read each time: both can be changed in Settings > TriMet. */
        poll_ms = trimet_config_get_poll_sec() * 1000;
        trimet_config_get_app_id(app_id, sizeof(app_id));
        if (app_id[0] == '\0') {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_status.state = TRIMET_STATE_NO_APP_ID;
            s_status.http_status = 0;
            strlcpy(s_status.error, "appID not set (Settings > TriMet)", sizeof(s_status.error));
            xSemaphoreGive(s_mutex);
            wait = portMAX_DELAY;   /* until trimet_config_set_app_id() calls refresh */
            continue;
        }

        state = fetch_once(app_id, s_work, &http_status, err, sizeof(err));

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_status.state = state;
        s_status.http_status = http_status;
        strlcpy(s_status.error, err, sizeof(s_status.error));
        if (state == TRIMET_STATE_OK) {
            memcpy(s_latest, s_work, sizeof(*s_latest));
            s_have_data = true;
            s_status.last_ok_uptime_ms = esp_timer_get_time() / 1000;
        }
        xSemaphoreGive(s_mutex);

        if (state == TRIMET_STATE_OK) {
            log_arrivals(s_work);
            retry_ms = RETRY_MIN_MS;
            wait = pdMS_TO_TICKS(poll_ms);
        } else {
            ESP_LOGW(TAG, "fetch failed (%s): %s; retry in %" PRIu32 " s",
                     state_name(state), err, retry_ms / 1000);
            if (state == TRIMET_STATE_HTTP_ERROR && http_status == 403) {
                ESP_LOGE(TAG, "appID rejected; check Settings > TriMet");
            }
            wait = pdMS_TO_TICKS(retry_ms);
            retry_ms = (retry_ms * 2 < poll_ms) ? retry_ms * 2 : poll_ms;
        }
    }
}

/* Runs on the default event loop. Fetch as soon as there is an IP address;
 * waiting only for Wi-Fi association made the first DNS lookup fail. */
static void net_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_have_ip = true;
        trimet_client_refresh();
    } else {
        /* WIFI_EVENT_STA_DISCONNECTED or IP_EVENT_STA_LOST_IP */
        s_have_ip = false;
    }
}

static bool sta_has_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;

    return netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

esp_err_t trimet_client_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    s_mutex  = xSemaphoreCreateMutex();
    s_latest = heap_caps_calloc(1, sizeof(*s_latest), MALLOC_CAP_SPIRAM);
    s_work   = heap_caps_calloc(1, sizeof(*s_work), MALLOC_CAP_SPIRAM);
    if (!s_mutex || !s_latest || !s_work) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(poll_task, "trimet", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    /* The default event loop is created by the template's Wi-Fi init. */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        net_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP,
                                                        net_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                        net_event_handler, NULL, NULL));

    /* In case the IP arrived before the handlers were registered. */
    if (sta_has_ip()) {
        s_have_ip = true;
        trimet_client_refresh();
    }
    ESP_LOGI(TAG, "started: streetcar stops %s, MAX stops %s, every %d s",
             CONFIG_TRIMET_STREETCAR_STOP_IDS, CONFIG_TRIMET_MAX_STOP_IDS, trimet_config_get_poll_sec());
    return ESP_OK;
}

bool trimet_client_get(trimet_arrivals_t *out, trimet_client_status_t *status)
{
    bool have;

    if (!s_mutex) {
        if (status) {
            memset(status, 0, sizeof(*status));
        }
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    have = s_have_data;
    if (out && have) {
        memcpy(out, s_latest, sizeof(*out));
    }
    if (status) {
        *status = s_status;
    }
    xSemaphoreGive(s_mutex);
    return have;
}

void trimet_client_refresh(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

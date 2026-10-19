/*
 * TriMet Arrivals V2 client: fetches arrivals for CONFIG_TRIMET_STOP_IDS every
 * CONFIG_TRIMET_POLL_INTERVAL_SEC while the network is up, and keeps the last
 * good result.
 */
#ifndef TRIMET_CLIENT_H
#define TRIMET_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "trimet_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRIMET_STATE_IDLE = 0,      /* no fetch attempted yet (waiting for network) */
    TRIMET_STATE_OK,            /* last fetch succeeded */
    TRIMET_STATE_NET_ERROR,     /* connection/TLS/timeout */
    TRIMET_STATE_HTTP_ERROR,    /* non-200 status (403 = bad appID) */
    TRIMET_STATE_API_ERROR,     /* resultSet.error in response */
    TRIMET_STATE_PARSE_ERROR,   /* malformed JSON */
    TRIMET_STATE_NO_APP_ID,     /* CONFIG_TRIMET_APP_ID is empty */
} trimet_state_t;

typedef struct {
    trimet_state_t state;            /* outcome of the most recent fetch */
    int            http_status;      /* of the most recent fetch, 0 if none */
    char           error[TRIMET_ERROR_LEN];
    int64_t        last_ok_uptime_ms; /* esp_timer uptime of last good fetch, 0 if never */
} trimet_client_status_t;

/* Start the polling task. Call after view_event_handle is created. */
esp_err_t trimet_client_start(void);

/*
 * Copy the last good arrivals (may be older than the latest attempt; see
 * status) into *out. Returns false if there has never been a good fetch.
 * Either pointer may be NULL.
 */
bool trimet_client_get(trimet_arrivals_t *out, trimet_client_status_t *status);

/* Wake the polling task to fetch now (e.g. on user request). */
void trimet_client_refresh(void);

#ifdef __cplusplus
}
#endif

#endif

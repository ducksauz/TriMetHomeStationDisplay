/*
 * Health: records why the device restarted (kept in NVS, so it can be read
 * on the Settings screen without a serial console), logs heap usage, and
 * restarts the device if arrivals stop updating for too long.
 *
 * Crashes and hangs are handled by ESP-IDF: a panic or task watchdog
 * timeout saves a core dump to flash and reboots (see sdkconfig.defaults).
 */
#ifndef TRIMET_HEALTH_H
#define TRIMET_HEALTH_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t           uptime_s;        /* this boot */
    esp_reset_reason_t boot_reason;     /* why this boot happened */
    uint32_t           crash_count;     /* unexpected restarts, kept in NVS */
    esp_reset_reason_t crash_reason;    /* the most recent unexpected restart */
    bool               crash_stale;     /* ...was the no-data restart below */
    uint32_t           crash_uptime_s;  /* how long it had been running, 0 if unknown */
    time_t             crash_time;      /* when it happened, 0 if unknown */
    bool               coredump;        /* a core dump is saved in flash */
} trimet_health_t;

/* Record the reset reason and start the periodic checks. Call after
 * indicator_model_init() (needs NVS) and trimet_client_start(). */
esp_err_t trimet_health_init(void);

void trimet_health_get(trimet_health_t *out);

/* Short description of a reset reason, e.g. "crash", "power on". */
const char *trimet_health_reason_name(esp_reset_reason_t reason, bool stale);

#ifdef __cplusplus
}
#endif

#endif

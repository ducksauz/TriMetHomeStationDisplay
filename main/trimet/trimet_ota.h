/*
 * Firmware updates over Wi-Fi (Settings > Firmware), started by hand.
 *
 * Check fetches version.txt from CONFIG_TRIMET_OTA_URL (by default the latest
 * GitHub release) and compares it with the running version; install
 * downloads trimet_station_display.bin into the other OTA slot and restarts.
 * The new image boots on probation: it is marked good once it has run for
 * a few minutes and fetched arrivals, and if it restarts before that the
 * bootloader goes back to the previous image.
 */
#ifndef TRIMET_OTA_H
#define TRIMET_OTA_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRIMET_OTA_VERSION_LEN  32

typedef enum {
    TRIMET_OTA_IDLE = 0,
    TRIMET_OTA_CHECKING,
    TRIMET_OTA_UP_TO_DATE,
    TRIMET_OTA_AVAILABLE,      /* latest is newer than the running version */
    TRIMET_OTA_DOWNLOADING,
    TRIMET_OTA_RESTARTING,
    TRIMET_OTA_ERROR,
} trimet_ota_state_t;

typedef struct {
    trimet_ota_state_t state;
    char               latest[TRIMET_OTA_VERSION_LEN];  /* from the last check */
    int                progress;                       /* percent, while downloading */
    char               error[64];
} trimet_ota_status_t;

/* Start the probation check if this is a newly installed image. Call after
 * trimet_client_start(). */
esp_err_t trimet_ota_init(void);

/* Start a check or an install in the background; ignored while one is
 * running. Install only runs after a check found a newer version. */
void trimet_ota_check(void);
void trimet_ota_install(void);

void trimet_ota_get(trimet_ota_status_t *out);

/* The running firmware version, e.g. "v1.1.0". */
const char *trimet_ota_running_version(void);

/* If the last installed update failed and the bootloader went back to this
 * image, copy that update's version into buf and return true. */
bool trimet_ota_rolled_back(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif

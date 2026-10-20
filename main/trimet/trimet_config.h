/*
 * TriMet settings that can be changed on the device (Settings > TriMet):
 * appID, poll interval and page display time. Saved in NVS; the Kconfig
 * values are the defaults. Readers call the getters each time, so changes
 * apply without a restart.
 */
#ifndef TRIMET_CONFIG_H
#define TRIMET_CONFIG_H

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRIMET_APP_ID_LEN       33      /* 32 chars + NUL */

#define TRIMET_POLL_MIN_SEC     30
#define TRIMET_POLL_MAX_SEC     120
#define TRIMET_POLL_STEP_SEC    30

#define TRIMET_ROTATE_MIN_SEC   5
#define TRIMET_ROTATE_MAX_SEC   30
#define TRIMET_ROTATE_STEP_SEC  5

/* Load the saved settings. Call after indicator_model_init() (needs NVS).
 * Until then the getters return the Kconfig defaults. */
esp_err_t trimet_config_init(void);

/* Copy the appID into buf (empty if none is set). */
void trimet_config_get_app_id(char *buf, size_t len);
int  trimet_config_get_poll_sec(void);
int  trimet_config_get_rotate_sec(void);

/* Save a setting. An empty appID reverts to the Kconfig one; out-of-range
 * intervals are clamped to the nearest step. */
void trimet_config_set_app_id(const char *app_id);
void trimet_config_set_poll_sec(int sec);
void trimet_config_set_rotate_sec(int sec);

#ifdef __cplusplus
}
#endif

#endif

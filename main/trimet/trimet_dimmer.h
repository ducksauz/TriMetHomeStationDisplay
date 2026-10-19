/*
 * Night dimming: lowers the backlight between two local times (default
 * 10 PM - 6 AM). The settings are saved in NVS.
 */
#ifndef TRIMET_DIMMER_H
#define TRIMET_DIMMER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRIMET_DIM_DAY_BRIGHTNESS    80   /* matches the template's default */
#define TRIMET_DIM_NIGHT_BRIGHTNESS  10   /* percent */

typedef struct {
    bool     enabled;
    uint16_t start_min;   /* minutes since local midnight */
    uint16_t end_min;
} trimet_dim_cfg_t;

/* Load the settings and start checking the time. Call after
 * indicator_model_init() (needs NVS and the display model). */
esp_err_t trimet_dimmer_init(void);

void trimet_dimmer_get(trimet_dim_cfg_t *cfg);

/* Save the settings and apply them immediately. */
void trimet_dimmer_set(const trimet_dim_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif

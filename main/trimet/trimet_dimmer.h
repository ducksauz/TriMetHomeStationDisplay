/*
 * Night dimming: lowers the backlight between two local times (default
 * 10 PM - 6 AM, to 10%). The settings are saved in NVS.
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
#define TRIMET_DIM_NIGHT_DEFAULT     10   /* percent */
#define TRIMET_DIM_NIGHT_MIN         1
#define TRIMET_DIM_NIGHT_MAX         50

typedef struct {
    bool     enabled;
    uint16_t start_min;   /* minutes since local midnight */
    uint16_t end_min;
    uint8_t  brightness;  /* night brightness, percent; added later, so last */
} trimet_dim_cfg_t;

/* Load the settings and start checking the time. Call after
 * indicator_model_init() (needs NVS and the display model). */
esp_err_t trimet_dimmer_init(void);

void trimet_dimmer_get(trimet_dim_cfg_t *cfg);

/* Save the settings and apply them immediately (also ends a preview). */
void trimet_dimmer_set(const trimet_dim_cfg_t *cfg);

/* Set the backlight to a brightness without saving it, so the night level
 * can be judged while adjusting it. trimet_dimmer_set() restores the right
 * level. */
void trimet_dimmer_preview(int brightness);

#ifdef __cplusplus
}
#endif

#endif

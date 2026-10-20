/*
 * Settings screen, opened from the cog on the arrivals screen: Wi-Fi (the
 * template's setup screen), night dimming, TriMet (appID, poll interval,
 * page time), and uptime/restart diagnostics.
 */
#ifndef TRIMET_SETTINGS_H
#define TRIMET_SETTINGS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the screen. back_target is where the back button returns.
 * Call with the LVGL lock held. */
void trimet_settings_init(lv_obj_t *back_target);

/* Show the settings screen. Call from the LVGL task. */
void trimet_settings_open(void);

#ifdef __cplusplus
}
#endif

#endif

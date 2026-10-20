/*
 * Settings screen, opened from the cog on the arrivals screen: Wi-Fi (the
 * template's setup screen), night dimming, TriMet (appID, poll interval,
 * page time), and Firmware (version, updates, uptime/restart diagnostics).
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

/* Show the App ID entry screen; back/save return to the arrivals screen.
 * Call from the LVGL task. */
void trimet_settings_open_app_id(void);

#ifdef __cplusplus
}
#endif

#endif

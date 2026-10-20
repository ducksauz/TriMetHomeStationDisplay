/*
 * Arrivals screen: Streetcar and MAX pages that rotate every page display
 * time (Settings > TriMet). Tapping the middle of the screen (inside a
 * TRIMET_VIEW_TAP_BORDER px border) switches pages; the border is left free
 * for other touch targets.
 */
#ifndef TRIMET_VIEW_H
#define TRIMET_VIEW_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRIMET_VIEW_TAP_BORDER 100

/* Create the screen and load it. Call with the LVGL lock held. */
void trimet_view_init(void);

/* The arrivals screen, e.g. as a "back" target for other screens. */
lv_obj_t *trimet_view_screen(void);

#ifdef __cplusplus
}
#endif

#endif

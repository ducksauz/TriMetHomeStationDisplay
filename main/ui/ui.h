// SquareLine LVGL GENERATED FILE
// EDITOR VERSION: SquareLine Studio 1.1.1
// LVGL VERSION: 8.3.3
// PROJECT: sensecap
//
// Trimmed for the TriMet display: the Wi-Fi and factory reset screens.

#ifndef _SENSECAP_UI_H
#define _SENSECAP_UI_H

#ifdef __cplusplus
extern "C" {
#endif

    #include "lvgl/lvgl.h"

extern lv_obj_t *ui_screen_wifi;
extern lv_obj_t *ui_wifi_st_6;
extern lv_obj_t *ui_wifi_title;
void ui_event_back3( lv_event_t * e);
extern lv_obj_t *ui_back3;
extern lv_obj_t *ui_screen_factory;
extern lv_obj_t *ui_factory_resetting_title;

LV_IMG_DECLARE( ui_img_wifi_disconet_png);   // assets/wifi_disconet.png
LV_IMG_DECLARE( ui_img_back_png);   // assets/back.png

LV_FONT_DECLARE( ui_font_font0);
LV_FONT_DECLARE( ui_font_font1);

void ui_init(void);
/* Where the Wi-Fi screen's back button returns to. */
void ui_set_screen_last(lv_obj_t *screen);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

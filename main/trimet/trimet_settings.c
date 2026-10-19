#include "trimet_settings.h"

#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"

#include "ui.h"
#include "ui_helpers.h"

#define SCREEN_W      480
#define MARGIN        16
#define ITEM_Y0       96
#define ITEM_H        64
#define ITEM_GAP      10
#define ANIM_MS       200

#define COLOR_BG      0x0B0E11
#define COLOR_ITEM    0x161B22
#define COLOR_PRESSED 0x262C36
#define COLOR_TEXT    0xFFFFFF
#define COLOR_DIM     0x8B949E

static lv_obj_t *s_scr;
static lv_obj_t *s_back_target;
static lv_obj_t *s_wifi_value;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

/* A clickable row: icon + title on the left, value + chevron on the right.
 * Returns the value label. */
static lv_obj_t *make_item(int index, const char *icon, const char *title, lv_event_cb_t cb)
{
    lv_obj_t *item = lv_obj_create(s_scr);
    lv_obj_t *l, *value;

    lv_obj_remove_style_all(item);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(item, SCREEN_W - 2 * MARGIN, ITEM_H);
    lv_obj_set_pos(item, MARGIN, ITEM_Y0 + index * (ITEM_H + ITEM_GAP));
    lv_obj_set_style_radius(item, 10, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(COLOR_ITEM), 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(item, cb, LV_EVENT_CLICKED, NULL);

    l = make_label(item, &lv_font_montserrat_22, COLOR_TEXT, icon);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 18, 0);
    l = make_label(item, &lv_font_montserrat_22, COLOR_TEXT, title);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 58, 0);

    l = make_label(item, &lv_font_montserrat_20, COLOR_DIM, LV_SYMBOL_RIGHT);
    lv_obj_align(l, LV_ALIGN_RIGHT_MID, -18, 0);
    value = make_label(item, &lv_font_montserrat_18, COLOR_DIM, "");
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_width(value, 200);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -48, 0);
    return value;
}

static void update_wifi_value(void)
{
    wifi_ap_record_t ap;
    char ssid[sizeof(ap.ssid) + 1];

    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        memcpy(ssid, ap.ssid, sizeof(ap.ssid));
        ssid[sizeof(ap.ssid)] = '\0';
        lv_label_set_text(s_wifi_value, ssid);
    } else {
        lv_label_set_text(s_wifi_value, "Not connected");
    }
}

static void back_cb(lv_event_t *e)
{
    _ui_screen_change(s_back_target, LV_SCR_LOAD_ANIM_MOVE_RIGHT, ANIM_MS, 0);
}

static void wifi_cb(lv_event_t *e)
{
    /* The Wi-Fi screen's back button returns here. */
    ui_set_screen_last(s_scr);
    _ui_screen_change(ui_screen_wifi, LV_SCR_LOAD_ANIM_OVER_BOTTOM, ANIM_MS, 0);
}

static void screen_loaded_cb(lv_event_t *e)
{
    update_wifi_value();
}

void trimet_settings_init(lv_obj_t *back_target)
{
    lv_obj_t *back, *l;

    s_back_target = back_target;

    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_scr, screen_loaded_cb, LV_EVENT_SCREEN_LOAD_START, NULL);

    /* Back: a large touch target in the top-left corner. */
    back = lv_obj_create(s_scr);
    lv_obj_remove_style_all(back);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(back, 80, 72);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    l = make_label(back, &lv_font_montserrat_28, COLOR_TEXT, LV_SYMBOL_LEFT);
    lv_obj_center(l);

    l = make_label(s_scr, &lv_font_montserrat_32, COLOR_TEXT, "Settings");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 20);

    s_wifi_value = make_item(0, LV_SYMBOL_WIFI, "Wi-Fi", wifi_cb);
}

void trimet_settings_open(void)
{
    _ui_screen_change(s_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
}

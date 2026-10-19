#include "trimet_settings.h"

#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"

#include "ui.h"
#include "ui_helpers.h"

#include "trimet_dimmer.h"
#include "trimet_format.h"

#define SCREEN_W      480
#define MARGIN        16
#define ITEM_Y0       96
#define ITEM_H        64
#define ITEM_GAP      10
#define ANIM_MS       200
#define MINUTE_STEP   15     /* dimming times are set in 15 minute steps */

#define COLOR_BG      0x0B0E11
#define COLOR_ITEM    0x161B22
#define COLOR_PRESSED 0x262C36
#define COLOR_TEXT    0xFFFFFF
#define COLOR_DIM     0x8B949E

static lv_obj_t *s_scr;
static lv_obj_t *s_back_target;
static lv_obj_t *s_wifi_value;
static lv_obj_t *s_dim_value;

/* Night dimming screen */
static lv_obj_t *s_dim_scr;
static lv_obj_t *s_dim_switch;
static lv_obj_t *s_dim_hour[2];    /* [0] start, [1] end */
static lv_obj_t *s_dim_minute[2];

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

static void update_dim_value(void)
{
    trimet_dim_cfg_t cfg;
    char start[12], end[12];

    trimet_dimmer_get(&cfg);
    if (!cfg.enabled) {
        lv_label_set_text(s_dim_value, "Off");
        return;
    }
    trimet_format_time_of_day(cfg.start_min, start, sizeof(start));
    trimet_format_time_of_day(cfg.end_min, end, sizeof(end));
    lv_label_set_text_fmt(s_dim_value, "%s - %s", start, end);
}

/* Top-left back button on a screen. */
static void make_back_button(lv_obj_t *scr, lv_event_cb_t cb)
{
    lv_obj_t *back = lv_obj_create(scr);
    lv_obj_t *l;

    lv_obj_remove_style_all(back);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(back, 80, 72);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, cb, LV_EVENT_CLICKED, NULL);
    l = make_label(back, &lv_font_montserrat_28, COLOR_TEXT, LV_SYMBOL_LEFT);
    lv_obj_center(l);
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

/* ---- night dimming screen ---- */

static void dim_controls_set_enabled(bool enabled)
{
    for (int i = 0; i < 2; i++) {
        if (enabled) {
            lv_obj_clear_state(s_dim_hour[i], LV_STATE_DISABLED);
            lv_obj_clear_state(s_dim_minute[i], LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_dim_hour[i], LV_STATE_DISABLED);
            lv_obj_add_state(s_dim_minute[i], LV_STATE_DISABLED);
        }
    }
}

/* Save whenever a control changes. */
static void dim_changed_cb(lv_event_t *e)
{
    trimet_dim_cfg_t cfg;

    cfg.enabled = lv_obj_has_state(s_dim_switch, LV_STATE_CHECKED);
    cfg.start_min = lv_roller_get_selected(s_dim_hour[0]) * 60 + lv_roller_get_selected(s_dim_minute[0]) * MINUTE_STEP;
    cfg.end_min = lv_roller_get_selected(s_dim_hour[1]) * 60 + lv_roller_get_selected(s_dim_minute[1]) * MINUTE_STEP;
    trimet_dimmer_set(&cfg);
    dim_controls_set_enabled(cfg.enabled);
}

static void dim_loaded_cb(lv_event_t *e)
{
    trimet_dim_cfg_t cfg;

    trimet_dimmer_get(&cfg);
    if (cfg.enabled) {
        lv_obj_add_state(s_dim_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_dim_switch, LV_STATE_CHECKED);
    }
    lv_roller_set_selected(s_dim_hour[0], cfg.start_min / 60, LV_ANIM_OFF);
    lv_roller_set_selected(s_dim_minute[0], (cfg.start_min % 60) / MINUTE_STEP, LV_ANIM_OFF);
    lv_roller_set_selected(s_dim_hour[1], cfg.end_min / 60, LV_ANIM_OFF);
    lv_roller_set_selected(s_dim_minute[1], (cfg.end_min % 60) / MINUTE_STEP, LV_ANIM_OFF);
    dim_controls_set_enabled(cfg.enabled);
}

static void dim_back_cb(lv_event_t *e)
{
    _ui_screen_change(s_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, ANIM_MS, 0);
}

static lv_obj_t *make_roller(const char *options, int width, int x, int y)
{
    lv_obj_t *r = lv_roller_create(s_dim_scr);

    lv_roller_set_options(r, options, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, width);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_22, 0);
    lv_obj_set_style_opa(r, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(r, dim_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return r;
}

static void build_dim_screen(void)
{
    static char hour_opts[24 * 6];   /* "12 AM\n1 AM\n...11 PM" */
    lv_obj_t *row, *l;
    size_t n = 0;

    for (int h = 0; h < 24; h++) {
        n += snprintf(hour_opts + n, sizeof(hour_opts) - n, "%s%d %s",
                      h ? "\n" : "", h % 12 == 0 ? 12 : h % 12, h < 12 ? "AM" : "PM");
    }

    s_dim_scr = make_screen();
    lv_obj_add_event_cb(s_dim_scr, dim_loaded_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    make_back_button(s_dim_scr, dim_back_cb);

    l = make_label(s_dim_scr, &lv_font_montserrat_32, COLOR_TEXT, "Night Dimming");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 20);

    /* Enable row */
    row = lv_obj_create(s_dim_scr);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SCREEN_W - 2 * MARGIN, ITEM_H);
    lv_obj_set_pos(row, MARGIN, ITEM_Y0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ITEM), 0);
    l = make_label(row, &lv_font_montserrat_22, COLOR_TEXT, "Dim at night");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 18, 0);
    s_dim_switch = lv_switch_create(row);
    lv_obj_set_size(s_dim_switch, 64, 34);
    lv_obj_align(s_dim_switch, LV_ALIGN_RIGHT_MID, -18, 0);
    lv_obj_add_event_cb(s_dim_switch, dim_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Start and end columns: hour + minute rollers each */
    for (int i = 0; i < 2; i++) {
        const int col_x = MARGIN + i * (SCREEN_W - 2 * MARGIN) / 2;
        const int col_w = (SCREEN_W - 2 * MARGIN) / 2;
        const int hour_w = 116, min_w = 76, gap = 8;
        const int x0 = col_x + (col_w - (hour_w + gap + min_w)) / 2;

        l = make_label(s_dim_scr, &lv_font_montserrat_20, COLOR_DIM, i == 0 ? "Dim at" : "Brighten at");
        lv_obj_set_width(l, col_w);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, col_x, 186);

        s_dim_hour[i] = make_roller(hour_opts, hour_w, x0, 218);
        s_dim_minute[i] = make_roller("00\n15\n30\n45", min_w, x0 + hour_w + gap, 218);
    }

    l = make_label(s_dim_scr, &lv_font_montserrat_16, COLOR_DIM, "");
    lv_label_set_text_fmt(l, "The screen dims to %d%% between these times.", TRIMET_DIM_NIGHT_BRIGHTNESS);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -40);
}

static void dim_cb(lv_event_t *e)
{
    _ui_screen_change(s_dim_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
}

/* ---- settings screen ---- */

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
    update_dim_value();
}

void trimet_settings_init(lv_obj_t *back_target)
{
    lv_obj_t *l;

    s_back_target = back_target;

    s_scr = make_screen();
    lv_obj_add_event_cb(s_scr, screen_loaded_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    make_back_button(s_scr, back_cb);

    l = make_label(s_scr, &lv_font_montserrat_32, COLOR_TEXT, "Settings");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 20);

    s_wifi_value = make_item(0, LV_SYMBOL_WIFI, "Wi-Fi", wifi_cb);
    s_dim_value = make_item(1, LV_SYMBOL_EYE_CLOSE, "Night dimming", dim_cb);

    build_dim_screen();
}

void trimet_settings_open(void)
{
    _ui_screen_change(s_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
}

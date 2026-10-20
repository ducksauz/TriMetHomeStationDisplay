#include "trimet_settings.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_wifi.h"
#include "sdkconfig.h"

#include "ui.h"
#include "ui_helpers.h"

#include "trimet_config.h"
#include "trimet_dimmer.h"
#include "trimet_format.h"
#include "trimet_health.h"
#include "trimet_ota.h"

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
#define COLOR_ACCENT  0x1F6FEB

static lv_obj_t *s_scr;
static lv_obj_t *s_back_target;
static lv_obj_t *s_wifi_value;
static lv_obj_t *s_dim_value;
static lv_obj_t *s_trimet_value;
static lv_obj_t *s_fw_value;

/* Night dimming screen */
static lv_obj_t *s_dim_scr;
static lv_obj_t *s_dim_switch;
static lv_obj_t *s_dim_hour[2];    /* [0] start, [1] end */
static lv_obj_t *s_dim_minute[2];
static lv_obj_t *s_dim_slider;
static lv_obj_t *s_dim_level;

/* TriMet screen */
static lv_obj_t *s_trimet_scr;
static lv_obj_t *s_appid_value;
static lv_obj_t *s_poll_choice;
static lv_obj_t *s_rotate_choice;

/* Firmware screen */
static lv_obj_t *s_fw_scr;
static lv_obj_t *s_fw_status;
static lv_obj_t *s_fw_btn;
static lv_obj_t *s_fw_btn_label;
static lv_obj_t *s_fw_bar;
static lv_obj_t *s_health;

/* App ID screen */
static lv_obj_t *s_appid_scr;
static lv_obj_t *s_appid_ta;
static lv_obj_t *s_appid_return;   /* where back/save go */

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
static lv_obj_t *make_item(lv_obj_t *scr, int index, const char *icon, const char *title, lv_event_cb_t cb)
{
    lv_obj_t *item = lv_obj_create(scr);
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

static void format_duration(uint32_t s, char *buf, size_t len)
{
    if (s >= 86400) {
        snprintf(buf, len, "%ud %uh", (unsigned)(s / 86400), (unsigned)(s % 86400 / 3600));
    } else if (s >= 3600) {
        snprintf(buf, len, "%uh %um", (unsigned)(s / 3600), (unsigned)(s % 3600 / 60));
    } else {
        snprintf(buf, len, "%um", (unsigned)(s / 60));
    }
}

/* Uptime and the most recent unexpected restart, so restarts can be noticed
 * without a serial console. */
static void update_health(void)
{
    trimet_health_t h;
    char up[16], ran[16], when[32] = "";

    trimet_health_get(&h);
    format_duration(h.uptime_s, up, sizeof(up));
    if (h.crash_count == 0) {
        lv_label_set_text_fmt(s_health, "Up %s\nNo unexpected restarts", up);
        return;
    }
    if (h.crash_time) {
        struct tm tm;
        localtime_r(&h.crash_time, &tm);
        strftime(when, sizeof(when), " on %b %d, %I:%M %p", &tm);
    }
    format_duration(h.crash_uptime_s, ran, sizeof(ran));
    lv_label_set_text_fmt(s_health, "Up %s\nUnexpected restarts: %u\nLast: %s%s%s%s%s",
                          up, (unsigned)h.crash_count,
                          trimet_health_reason_name(h.crash_reason, h.crash_stale), when,
                          h.crash_uptime_s ? " after " : "", h.crash_uptime_s ? ran : "",
                          h.coredump ? "\nCore dump saved" : "");
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

static void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (enabled) {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
}

static void dim_controls_set_enabled(bool enabled)
{
    for (int i = 0; i < 2; i++) {
        set_enabled(s_dim_hour[i], enabled);
        set_enabled(s_dim_minute[i], enabled);
    }
    set_enabled(s_dim_slider, enabled);
}

/* Save whenever a control changes. */
static void dim_changed_cb(lv_event_t *e)
{
    trimet_dim_cfg_t cfg;

    cfg.enabled = lv_obj_has_state(s_dim_switch, LV_STATE_CHECKED);
    cfg.start_min = lv_roller_get_selected(s_dim_hour[0]) * 60 + lv_roller_get_selected(s_dim_minute[0]) * MINUTE_STEP;
    cfg.end_min = lv_roller_get_selected(s_dim_hour[1]) * 60 + lv_roller_get_selected(s_dim_minute[1]) * MINUTE_STEP;
    cfg.brightness = lv_slider_get_value(s_dim_slider);
    trimet_dimmer_set(&cfg);
    dim_controls_set_enabled(cfg.enabled);
}

/* While dragging, show the level on the backlight; it is saved (and the
 * right level restored) on release. */
static void dim_slider_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(s_dim_slider);

    lv_label_set_text_fmt(s_dim_level, "%d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        trimet_dimmer_preview(v);
    } else {
        dim_changed_cb(e);
    }
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
    lv_slider_set_value(s_dim_slider, cfg.brightness, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_dim_level, "%d%%", cfg.brightness);
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

    /* Night brightness row */
    row = lv_obj_create(s_dim_scr);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SCREEN_W - 2 * MARGIN, ITEM_H);
    lv_obj_set_pos(row, MARGIN, 368);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ITEM), 0);
    l = make_label(row, &lv_font_montserrat_22, COLOR_TEXT, "Brightness");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 18, 0);
    s_dim_level = make_label(row, &lv_font_montserrat_20, COLOR_DIM, "");
    lv_obj_set_width(s_dim_level, 56);
    lv_obj_set_style_text_align(s_dim_level, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_dim_level, LV_ALIGN_RIGHT_MID, -18, 0);
    s_dim_slider = lv_slider_create(row);
    lv_slider_set_range(s_dim_slider, TRIMET_DIM_NIGHT_MIN, TRIMET_DIM_NIGHT_MAX);
    lv_obj_set_size(s_dim_slider, 190, 12);
    lv_obj_align(s_dim_slider, LV_ALIGN_RIGHT_MID, -100, 0);
    lv_obj_set_ext_click_area(s_dim_slider, 20);
    lv_obj_set_style_opa(s_dim_slider, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_dim_slider, dim_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_dim_slider, dim_slider_cb, LV_EVENT_RELEASED, NULL);
}

static void dim_cb(lv_event_t *e)
{
    _ui_screen_change(s_dim_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
}

/* ---- App ID screen ---- */

static void appid_close(void)
{
    _ui_screen_change(s_appid_return, LV_SCR_LOAD_ANIM_MOVE_RIGHT, ANIM_MS, 0);
}

static void appid_back_cb(lv_event_t *e)
{
    appid_close();   /* discard edits */
}

/* Keyboard: the check key saves, the keyboard key cancels. */
static void appid_kb_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        trimet_config_set_app_id(lv_textarea_get_text(s_appid_ta));
    }
    appid_close();
}

static void appid_loaded_cb(lv_event_t *e)
{
    char app_id[TRIMET_APP_ID_LEN];

    trimet_config_get_app_id(app_id, sizeof(app_id));
    lv_textarea_set_text(s_appid_ta, app_id);
}

static void build_appid_screen(void)
{
    lv_obj_t *l, *kb;

    s_appid_scr = make_screen();
    lv_obj_add_event_cb(s_appid_scr, appid_loaded_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    make_back_button(s_appid_scr, appid_back_cb);

    l = make_label(s_appid_scr, &lv_font_montserrat_32, COLOR_TEXT, "TriMet App ID");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 20);

    s_appid_ta = lv_textarea_create(s_appid_scr);
    lv_textarea_set_one_line(s_appid_ta, true);
    lv_textarea_set_max_length(s_appid_ta, TRIMET_APP_ID_LEN - 1);
    /* Letters and digits only: the appID goes into the request URL as is. */
    lv_textarea_set_accepted_chars(s_appid_ta,
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    lv_textarea_set_placeholder_text(s_appid_ta, "Not set");
    lv_obj_set_style_text_font(s_appid_ta, &lv_font_montserrat_22, 0);
    lv_obj_set_width(s_appid_ta, SCREEN_W - 2 * MARGIN);
    lv_obj_set_pos(s_appid_ta, MARGIN, 88);
    lv_obj_add_state(s_appid_ta, LV_STATE_FOCUSED);   /* show the cursor */

    l = make_label(s_appid_scr, &lv_font_montserrat_16, COLOR_DIM,
                   "Register for one at developer.trimet.org/appid");
    lv_obj_set_width(l, SCREEN_W - 2 * MARGIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, MARGIN, 150);

    kb = lv_keyboard_create(s_appid_scr);
    lv_keyboard_set_textarea(kb, s_appid_ta);
    lv_obj_set_size(kb, SCREEN_W, 270);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, appid_kb_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, appid_kb_cb, LV_EVENT_CANCEL, NULL);
}

/* ---- TriMet screen ---- */

static const char *POLL_MAP[] = {"30 s", "60 s", "90 s", "120 s", ""};
static const char *ROTATE_MAP[] = {"5 s", "10 s", "15 s", "20 s", "25 s", "30 s", ""};

static void update_appid_value(lv_obj_t *label)
{
    char app_id[TRIMET_APP_ID_LEN];
    size_t n;

    trimet_config_get_app_id(app_id, sizeof(app_id));
    n = strlen(app_id);
    if (n == 0) {
        lv_label_set_text(label, "Not set");
    } else {
        /* Only the end, so the full secret is never on screen here. */
        lv_label_set_text_fmt(label, "Ends in %s", app_id + (n > 4 ? n - 4 : 0));
    }
}

/* A row of mutually exclusive buttons, one per option in map. */
static lv_obj_t *make_choice(const char **map, int y, lv_event_cb_t cb)
{
    lv_obj_t *m = lv_btnmatrix_create(s_trimet_scr);

    lv_btnmatrix_set_map(m, map);
    lv_btnmatrix_set_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(m, true);
    lv_obj_set_size(m, SCREEN_W - 2 * MARGIN, 60);
    lv_obj_set_pos(m, MARGIN, y);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_column(m, 8, 0);
    lv_obj_set_style_radius(m, 10, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, lv_color_hex(COLOR_ITEM), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, lv_color_hex(COLOR_PRESSED), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(m, lv_color_hex(COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(m, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return m;
}

static void select_choice(lv_obj_t *m, int index)
{
    lv_btnmatrix_set_btn_ctrl(m, index, LV_BTNMATRIX_CTRL_CHECKED);   /* one_checked clears the rest */
}

static void poll_cb(lv_event_t *e)
{
    uint16_t i = lv_btnmatrix_get_selected_btn(s_poll_choice);

    if (i != LV_BTNMATRIX_BTN_NONE) {
        trimet_config_set_poll_sec(TRIMET_POLL_MIN_SEC + i * TRIMET_POLL_STEP_SEC);
    }
    select_choice(s_poll_choice, (trimet_config_get_poll_sec() - TRIMET_POLL_MIN_SEC) / TRIMET_POLL_STEP_SEC);
}

static void rotate_cb(lv_event_t *e)
{
    uint16_t i = lv_btnmatrix_get_selected_btn(s_rotate_choice);

    if (i != LV_BTNMATRIX_BTN_NONE) {
        trimet_config_set_rotate_sec(TRIMET_ROTATE_MIN_SEC + i * TRIMET_ROTATE_STEP_SEC);
    }
    select_choice(s_rotate_choice, (trimet_config_get_rotate_sec() - TRIMET_ROTATE_MIN_SEC) / TRIMET_ROTATE_STEP_SEC);
}

static void trimet_loaded_cb(lv_event_t *e)
{
    update_appid_value(s_appid_value);
    select_choice(s_poll_choice, (trimet_config_get_poll_sec() - TRIMET_POLL_MIN_SEC) / TRIMET_POLL_STEP_SEC);
    select_choice(s_rotate_choice, (trimet_config_get_rotate_sec() - TRIMET_ROTATE_MIN_SEC) / TRIMET_ROTATE_STEP_SEC);
}

static void trimet_back_cb(lv_event_t *e)
{
    _ui_screen_change(s_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, ANIM_MS, 0);
}

static void appid_cb(lv_event_t *e)
{
    s_appid_return = s_trimet_scr;
    _ui_screen_change(s_appid_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
}

static void build_trimet_screen(void)
{
    lv_obj_t *l;

    s_trimet_scr = make_screen();
    lv_obj_add_event_cb(s_trimet_scr, trimet_loaded_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    make_back_button(s_trimet_scr, trimet_back_cb);

    l = make_label(s_trimet_scr, &lv_font_montserrat_32, COLOR_TEXT, "TriMet");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 20);

    s_appid_value = make_item(s_trimet_scr, 0, LV_SYMBOL_EDIT, "App ID", appid_cb);

    l = make_label(s_trimet_scr, &lv_font_montserrat_20, COLOR_DIM, "Check for arrivals every");
    lv_obj_set_pos(l, MARGIN + 4, 190);
    s_poll_choice = make_choice(POLL_MAP, 220, poll_cb);

    l = make_label(s_trimet_scr, &lv_font_montserrat_20, COLOR_DIM, "Show each page for");
    lv_obj_set_pos(l, MARGIN + 4, 310);
    s_rotate_choice = make_choice(ROTATE_MAP, 340, rotate_cb);

    build_appid_screen();
}

static void trimet_cb(lv_event_t *e)
{
    _ui_screen_change(s_trimet_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
}

/* ---- firmware screen ---- */

static void fw_btn_cb(lv_event_t *e)
{
    trimet_ota_status_t st;

    trimet_ota_get(&st);
    if (st.state == TRIMET_OTA_AVAILABLE) {
        trimet_ota_install();
    } else {
        trimet_ota_check();
    }
}

/* Runs every 250 ms; only does work while the screen is shown. */
static void fw_timer_cb(lv_timer_t *t)
{
    trimet_ota_status_t st;
    char bad[TRIMET_OTA_VERSION_LEN];
    bool button = true;

    if (lv_scr_act() != s_fw_scr) {
        return;
    }
    trimet_ota_get(&st);
    lv_obj_add_flag(s_fw_bar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_fw_btn_label, "Check for update");

    switch (st.state) {
    case TRIMET_OTA_IDLE:
        if (trimet_ota_rolled_back(bad, sizeof(bad))) {
            lv_label_set_text_fmt(s_fw_status, "Update to %s failed and was undone.", bad);
        } else {
            lv_label_set_text(s_fw_status, "");
        }
        break;
    case TRIMET_OTA_CHECKING:
        lv_label_set_text(s_fw_status, "Checking...");
        button = false;
        break;
    case TRIMET_OTA_UP_TO_DATE:
        lv_label_set_text_fmt(s_fw_status, "Up to date (latest is %s).", st.latest);
        break;
    case TRIMET_OTA_AVAILABLE:
        lv_label_set_text_fmt(s_fw_status, "%s is available.", st.latest);
        lv_label_set_text_fmt(s_fw_btn_label, "Install %s", st.latest);
        break;
    case TRIMET_OTA_DOWNLOADING:
        lv_label_set_text_fmt(s_fw_status, "Downloading %s... %d%%", st.latest, st.progress);
        lv_bar_set_value(s_fw_bar, st.progress, LV_ANIM_OFF);
        lv_obj_clear_flag(s_fw_bar, LV_OBJ_FLAG_HIDDEN);
        button = false;
        break;
    case TRIMET_OTA_RESTARTING:
        lv_label_set_text(s_fw_status, "Installed. Restarting...");
        button = false;
        break;
    case TRIMET_OTA_ERROR:
        lv_label_set_text(s_fw_status, st.error);
        lv_label_set_text(s_fw_btn_label, "Try again");
        break;
    }
    if (button) {
        lv_obj_clear_flag(s_fw_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_fw_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void fw_loaded_cb(lv_event_t *e)
{
    update_health();
    fw_timer_cb(NULL);
}

static void fw_back_cb(lv_event_t *e)
{
    _ui_screen_change(s_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, ANIM_MS, 0);
}

static void build_fw_screen(void)
{
    lv_obj_t *l;

    s_fw_scr = make_screen();
    lv_obj_add_event_cb(s_fw_scr, fw_loaded_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    make_back_button(s_fw_scr, fw_back_cb);

    l = make_label(s_fw_scr, &lv_font_montserrat_32, COLOR_TEXT, "Firmware");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 20);

    l = make_label(s_fw_scr, &lv_font_montserrat_22, COLOR_TEXT, "");
    lv_label_set_text_fmt(l, "Version %s", trimet_ota_running_version());
    lv_obj_set_width(l, SCREEN_W - 2 * MARGIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, MARGIN, 100);

    s_fw_status = make_label(s_fw_scr, &lv_font_montserrat_18, COLOR_DIM, "");
    lv_label_set_long_mode(s_fw_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_fw_status, SCREEN_W - 2 * MARGIN);
    lv_obj_set_style_text_align(s_fw_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_fw_status, MARGIN, 144);

    s_fw_bar = lv_bar_create(s_fw_scr);
    lv_obj_set_size(s_fw_bar, SCREEN_W - 4 * MARGIN, 16);
    lv_obj_set_pos(s_fw_bar, 2 * MARGIN, 210);
    lv_bar_set_range(s_fw_bar, 0, 100);
    lv_obj_set_style_bg_color(s_fw_bar, lv_color_hex(COLOR_ITEM), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_fw_bar, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_add_flag(s_fw_bar, LV_OBJ_FLAG_HIDDEN);

    s_fw_btn = lv_btn_create(s_fw_scr);
    lv_obj_set_size(s_fw_btn, 280, 64);
    lv_obj_align(s_fw_btn, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_set_style_radius(s_fw_btn, 10, 0);
    lv_obj_set_style_shadow_width(s_fw_btn, 0, 0);
    lv_obj_set_style_bg_color(s_fw_btn, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(s_fw_btn, fw_btn_cb, LV_EVENT_CLICKED, NULL);
    s_fw_btn_label = make_label(s_fw_btn, &lv_font_montserrat_22, COLOR_TEXT, "Check for update");
    lv_obj_center(s_fw_btn_label);

    s_health = make_label(s_fw_scr, &lv_font_montserrat_16, COLOR_DIM, "");
    lv_obj_set_width(s_health, SCREEN_W - 2 * MARGIN);
    lv_obj_set_style_text_align(s_health, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_health, LV_ALIGN_BOTTOM_MID, 0, -24);

    lv_timer_create(fw_timer_cb, 250, NULL);
}

static void fw_cb(lv_event_t *e)
{
    _ui_screen_change(s_fw_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
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
    update_appid_value(s_trimet_value);
    lv_label_set_text(s_fw_value, trimet_ota_running_version());
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

    s_wifi_value = make_item(s_scr, 0, LV_SYMBOL_WIFI, "Wi-Fi", wifi_cb);
    s_dim_value = make_item(s_scr, 1, LV_SYMBOL_EYE_CLOSE, "Night dimming", dim_cb);
    s_trimet_value = make_item(s_scr, 2, LV_SYMBOL_GPS, "TriMet", trimet_cb);
    s_fw_value = make_item(s_scr, 3, LV_SYMBOL_DOWNLOAD, "Firmware", fw_cb);

    build_dim_screen();
    build_trimet_screen();
    build_fw_screen();
}

void trimet_settings_open(void)
{
    _ui_screen_change(s_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
}

void trimet_settings_open_app_id(void)
{
    s_appid_return = s_back_target;
    _ui_screen_change(s_appid_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, ANIM_MS, 0);
}

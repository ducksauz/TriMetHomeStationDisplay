#include "trimet_view.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "trimet_client.h"
#include "trimet_config.h"
#include "trimet_format.h"
#include "trimet_settings.h"
#include "ui.h"

#define SCREEN_W        480
#define SCREEN_H        480
#define MARGIN          16

#define SECTION_Y0      70
#define BODY_BOTTOM     442    /* sections must end above the footer */

#define STOPS_PER_PAGE  2      /* longer stop lists continue on another page */
#define MAX_PAGES       6
#define MAX_ROWS        3      /* arrivals per stop */
#define MAX_MINUTES     60     /* hide arrivals further out than this */
#define REFRESH_MS      1000

#define COG_W           64     /* settings button hit area, bottom-right corner */
#define COG_H           44

#define COLOR_BG        0x0B0E11
#define COLOR_TEXT      0xFFFFFF
#define COLOR_SUBTLE    0xC9D1D9
#define COLOR_DIM       0x8B949E
#define COLOR_WARN      0xF0A030
#define COLOR_ERROR     0xE5534B
#define COLOR_DOT_OFF   0x3A4048
#define COLOR_ACCENT    0x1F6FEB

static const char *TAG = "trimet-view";

/* Section geometry and fonts. */
typedef struct {
    int               rows;        /* arrivals per stop */
    int               hdr_h;       /* stop name row */
    int               row_h;
    int               gap;         /* between sections */
    int               badge_w, badge_h;
    int               eta_w;
    int               empty_y;     /* "No arrivals" offset below the header */
    const lv_font_t  *name_font, *dir_font, *badge_font, *dest_font, *eta_font, *empty_font;
} layout_t;

static const layout_t LAYOUT = {
    .rows = MAX_ROWS, .hdr_h = 32, .row_h = 50, .gap = 6,
    .badge_w = 92, .badge_h = 38, .eta_w = 112, .empty_y = 14,
    .name_font = &lv_font_montserrat_20, .dir_font = &lv_font_montserrat_16,
    .badge_font = &lv_font_montserrat_20, .dest_font = &lv_font_montserrat_22,
    .eta_font = &lv_font_montserrat_28, .empty_font = &lv_font_montserrat_18,
};

typedef struct {
    lv_obj_t *row;
    lv_obj_t *badge;
    lv_obj_t *badge_label;
    lv_obj_t *dest;
    lv_obj_t *eta;
} row_t;

typedef struct {
    lv_obj_t *cont;
    lv_obj_t *name;
    lv_obj_t *dir;
    lv_obj_t *empty;
    row_t     rows[MAX_ROWS];
} section_t;

typedef struct {
    const char     *title;
    trimet_mode_t   mode;
    uint32_t        stops[STOPS_PER_PAGE];
    int             stop_count;
    lv_obj_t       *cont;
    section_t       sections[STOPS_PER_PAGE];
} page_t;

static page_t     s_pages[MAX_PAGES];
static int        s_page_count;
static int        s_page;
static lv_obj_t  *s_scr;
static lv_obj_t  *s_title;
static lv_obj_t  *s_clock;
static lv_obj_t  *s_status;
static lv_obj_t  *s_legend;
static lv_obj_t  *s_dots[MAX_PAGES];
static lv_timer_t *s_rotate_timer;
static lv_obj_t  *s_setup;          /* "enter your App ID" panel */
static lv_obj_t  *s_setup_title;
static lv_obj_t  *s_setup_body;
static trimet_arrivals_t *s_data;   /* PSRAM copy of the client's snapshot */

/* ---- small helpers ---- */

/* A bare container: no theme styling, not clickable or scrollable, so taps
 * fall through to the screen. */
static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

/* Avoid invalidating (and redrawing) labels whose text did not change. */
static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void set_text_color(lv_obj_t *obj, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);
    if (lv_obj_get_style_text_color(obj, 0).full != c.full) {
        lv_obj_set_style_text_color(obj, c, 0);
    }
}

static void set_bg_color(lv_obj_t *obj, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);
    if (lv_obj_get_style_bg_color(obj, 0).full != c.full) {
        lv_obj_set_style_bg_color(obj, c, 0);
    }
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != hidden) {
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static uint32_t eta_color(trimet_status_t st)
{
    switch (st) {
    case TRIMET_STATUS_SCHEDULED: return COLOR_DIM;
    case TRIMET_STATUS_DELAYED:   return COLOR_WARN;
    case TRIMET_STATUS_CANCELED:  return COLOR_ERROR;
    default:                      return COLOR_TEXT;
    }
}

/* ---- rendering ---- */

static void render_clock(void)
{
    char buf[16] = "";
    time_t now = time(NULL);
    struct tm tm;

    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 >= 2024) {   /* only once SNTP has set the clock */
        strftime(buf, sizeof(buf), "%I:%M %p", &tm);
        if (buf[0] == '0') {
            memmove(buf, buf + 1, strlen(buf));
        }
    }
    set_text(s_clock, buf);
}

static void render_section(const page_t *page, section_t *sec, uint32_t loc_id,
                           bool have_data, int64_t now_ms, bool *any_scheduled)
{
    const trimet_arrival_t *sel[MAX_ROWS];
    const int rows = LAYOUT.rows;
    const trimet_location_t *loc = NULL;
    char name[TRIMET_DESC_LEN];
    int n = 0;

    if (have_data) {
        loc = trimet_find_location(s_data, loc_id);
        n = trimet_select(s_data, loc_id, page->mode, now_ms, MAX_MINUTES, sel, rows);
    }
    if (have_data) {
        trimet_format_stop_name(s_data, loc_id, name, sizeof(name));
    } else {
        snprintf(name, sizeof(name), "Stop %" PRIu32, loc_id);
    }
    set_text(sec->name, name);
    set_text(sec->dir, loc ? loc->dir : "");

    for (int i = 0; i < rows; i++) {
        row_t *r = &sec->rows[i];
        char badge[TRIMET_BADGE_LEN], dest[TRIMET_SIGN_LEN], eta[16];
        uint32_t color;

        if (i >= n) {
            set_hidden(r->row, true);
            continue;
        }
        trimet_split_sign(sel[i], badge, sizeof(badge), dest, sizeof(dest));
        trimet_format_eta(sel[i], now_ms, eta, sizeof(eta));
        color = trimet_badge_color(sel[i], badge);

        set_bg_color(r->badge, color);
        set_text_color(r->badge_label, trimet_color_is_light(color) ? 0x000000 : 0xFFFFFF);
        set_text(r->badge_label, badge);
        set_text(r->dest, dest);
        set_text(r->eta, eta);
        set_text_color(r->eta, eta_color(sel[i]->status));
        set_hidden(r->row, false);
        if (sel[i]->status == TRIMET_STATUS_SCHEDULED) {
            *any_scheduled = true;
        }
    }

    set_hidden(sec->empty, !have_data || n > 0);
}

static void render_status(bool have_data, const trimet_client_status_t *st)
{
    char buf[TRIMET_ERROR_LEN + 32];
    uint32_t color = COLOR_DIM;
    int64_t age_s = have_data ? (esp_timer_get_time() / 1000 - st->last_ok_uptime_ms) / 1000 : 0;
    bool failing = st->state != TRIMET_STATE_OK && st->state != TRIMET_STATE_IDLE;

    if (st->state == TRIMET_STATE_NO_APP_ID) {
        snprintf(buf, sizeof(buf), "No App ID");
        color = COLOR_ERROR;
    } else if (!have_data) {
        if (failing) {
            snprintf(buf, sizeof(buf), "%s", st->error);
            color = COLOR_ERROR;
        } else {
            snprintf(buf, sizeof(buf), "Waiting for network...");
        }
    } else {
        if (age_s < 120) {
            snprintf(buf, sizeof(buf), "Updated %ds ago%s", (int)age_s, failing ? " - retrying" : "");
        } else {
            snprintf(buf, sizeof(buf), "Updated %d min ago%s", (int)(age_s / 60), failing ? " - retrying" : "");
        }
        /* Stale: more than two missed polls. */
        if (age_s > 2 * trimet_config_get_poll_sec() + 10) {
            color = COLOR_WARN;
        }
    }
    set_text(s_status, buf);
    set_text_color(s_status, color);
}

/* Without a working appID there is nothing to show but HTTP errors, so ask
 * for one instead. Returns true if the panel is shown. */
static bool render_setup(const trimet_client_status_t *st)
{
    bool rejected = st->state == TRIMET_STATE_HTTP_ERROR && st->http_status == 403;
    bool show = rejected || st->state == TRIMET_STATE_NO_APP_ID;

    set_hidden(s_setup, !show);
    if (!show) {
        return false;
    }
    set_text(s_title, "TriMet");
    if (rejected) {
        set_text(s_setup_title, "TriMet rejected the App ID");
        set_text(s_setup_body, "Check that it was entered correctly.");
    } else {
        set_text(s_setup_title, "Enter your TriMet App ID");
        set_text(s_setup_body, "Arrivals need a free developer App ID.\n"
                               "Register at developer.trimet.org/appid");
    }
    for (int i = 0; i < s_page_count; i++) {
        set_hidden(s_pages[i].cont, true);
    }
    set_hidden(s_legend, true);
    return true;
}

static void render(void)
{
    trimet_client_status_t st;
    bool have_data = trimet_client_get(s_data, &st);
    page_t *page = &s_pages[s_page];
    int64_t now_ms = 0;
    bool any_scheduled = false;

    if (render_setup(&st)) {
        render_status(have_data, &st);
        render_clock();
        return;
    }

    if (have_data) {
        /* Server time now = response time + time elapsed since the fetch. */
        now_ms = s_data->query_time_ms + (esp_timer_get_time() / 1000 - st.last_ok_uptime_ms);
    }

    set_text(s_title, page->title);
    for (int i = 0; i < s_page_count; i++) {
        set_bg_color(s_dots[i], i == s_page ? COLOR_TEXT : COLOR_DOT_OFF);
        set_hidden(s_pages[i].cont, i != s_page);
    }
    for (int i = 0; i < page->stop_count; i++) {
        render_section(page, &page->sections[i], page->stops[i], have_data, now_ms, &any_scheduled);
    }
    set_hidden(s_legend, !any_scheduled);
    render_status(have_data, &st);
    render_clock();
}

static void show_page(int page)
{
    s_page = page;
    render();
}

static int next_page(void)
{
    return s_page_count > 0 ? (s_page + 1) % s_page_count : 0;
}

/* ---- events and timers (run in the LVGL task, lock held) ---- */

static void refresh_timer_cb(lv_timer_t *t)
{
    /* Pick up a page time change from Settings > TriMet. */
    uint32_t rotate_ms = trimet_config_get_rotate_sec() * 1000;
    if (s_rotate_timer->period != rotate_ms) {
        lv_timer_set_period(s_rotate_timer, rotate_ms);
    }

    if (lv_scr_act() == s_scr) {
        render();
    }
}

static void rotate_timer_cb(lv_timer_t *t)
{
    if (lv_scr_act() == s_scr) {
        show_page(next_page());
    }
}

static void cog_event_cb(lv_event_t *e)
{
    trimet_settings_open();
}

static void setup_btn_cb(lv_event_t *e)
{
    trimet_settings_open_app_id();
}

static void screen_event_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_t *indev = lv_indev_get_act();

    if (!indev) {
        return;
    }
    lv_indev_get_point(indev, &p);
    if (p.x >= TRIMET_VIEW_TAP_BORDER && p.x < SCREEN_W - TRIMET_VIEW_TAP_BORDER &&
        p.y >= TRIMET_VIEW_TAP_BORDER && p.y < SCREEN_H - TRIMET_VIEW_TAP_BORDER) {
        show_page(next_page());
        lv_timer_reset(s_rotate_timer);   /* give the new page a full interval */
    }
}

/* ---- construction ---- */

static void build_section(page_t *page, section_t *sec, int index)
{
    const layout_t *L = &LAYOUT;
    const int sec_h = L->hdr_h + L->rows * L->row_h;

    sec->cont = plain_obj(page->cont);
    lv_obj_set_pos(sec->cont, 0, index * (sec_h + L->gap));
    lv_obj_set_size(sec->cont, SCREEN_W, sec_h);

    sec->name = make_label(sec->cont, L->name_font, COLOR_SUBTLE);
    lv_label_set_long_mode(sec->name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sec->name, SCREEN_W - 2 * MARGIN - 120);
    lv_obj_align(sec->name, LV_ALIGN_TOP_LEFT, MARGIN, (L->hdr_h - lv_font_get_line_height(L->name_font)) / 2);

    sec->dir = make_label(sec->cont, L->dir_font, COLOR_DIM);
    lv_obj_align(sec->dir, LV_ALIGN_TOP_RIGHT, -MARGIN, (L->hdr_h - lv_font_get_line_height(L->dir_font)) / 2);

    sec->empty = make_label(sec->cont, L->empty_font, COLOR_DIM);
    lv_label_set_text_fmt(sec->empty, "No arrivals in the next %d min", MAX_MINUTES);
    lv_obj_set_pos(sec->empty, MARGIN, L->hdr_h + L->empty_y);

    for (int i = 0; i < L->rows; i++) {
        row_t *r = &sec->rows[i];

        r->row = plain_obj(sec->cont);
        lv_obj_set_pos(r->row, 0, L->hdr_h + i * L->row_h);
        lv_obj_set_size(r->row, SCREEN_W, L->row_h);

        r->badge = plain_obj(r->row);
        lv_obj_set_size(r->badge, L->badge_w, L->badge_h);
        lv_obj_align(r->badge, LV_ALIGN_LEFT_MID, MARGIN, 0);
        lv_obj_set_style_radius(r->badge, L->badge_h / 5, 0);
        lv_obj_set_style_bg_opa(r->badge, LV_OPA_COVER, 0);

        r->badge_label = make_label(r->badge, L->badge_font, COLOR_TEXT);
        lv_obj_center(r->badge_label);

        r->dest = make_label(r->row, L->dest_font, COLOR_TEXT);
        lv_label_set_long_mode(r->dest, LV_LABEL_LONG_DOT);
        lv_obj_set_width(r->dest, SCREEN_W - (MARGIN + L->badge_w + 12) - L->eta_w - MARGIN);
        lv_obj_align(r->dest, LV_ALIGN_LEFT_MID, MARGIN + L->badge_w + 12, 0);

        r->eta = make_label(r->row, L->eta_font, COLOR_TEXT);
        lv_obj_set_width(r->eta, L->eta_w);
        lv_obj_set_style_text_align(r->eta, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(r->eta, LV_ALIGN_RIGHT_MID, -MARGIN, 0);

        lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_page(page_t *page)
{
    page->cont = plain_obj(s_scr);
    lv_obj_set_pos(page->cont, 0, SECTION_Y0);
    lv_obj_set_size(page->cont, SCREEN_W, BODY_BOTTOM - SECTION_Y0);
    lv_obj_add_flag(page->cont, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < page->stop_count; i++) {
        build_section(page, &page->sections[i], i);
    }
}

/* Add pages for a stop list, STOPS_PER_PAGE stops each, all with the same title. */
static void add_pages(const char *title, trimet_mode_t mode, const char *stop_ids)
{
    uint32_t ids[MAX_PAGES * STOPS_PER_PAGE];
    int n = trimet_parse_id_list(stop_ids, ids, MAX_PAGES * STOPS_PER_PAGE);

    for (int i = 0; i < n && s_page_count < MAX_PAGES; i += STOPS_PER_PAGE) {
        page_t *page = &s_pages[s_page_count++];
        page->title = title;
        page->mode = mode;
        for (int j = i; j < n && j < i + STOPS_PER_PAGE; j++) {
            page->stops[page->stop_count++] = ids[j];
        }
    }
}

static void init_pages(void)
{
    add_pages("Streetcar", TRIMET_MODE_STREETCAR, CONFIG_TRIMET_STREETCAR_STOP_IDS);
    add_pages("MAX", TRIMET_MODE_LIGHT_RAIL, CONFIG_TRIMET_MAX_STOP_IDS);
    s_page = 0;
}

void trimet_view_init(void)
{
    s_data = heap_caps_calloc(1, sizeof(*s_data), MALLOC_CAP_SPIRAM);
    if (!s_data) {
        ESP_LOGE(TAG, "out of memory");
        return;
    }
    init_pages();
    if (s_page_count == 0) {
        ESP_LOGE(TAG, "no stops configured");
        return;
    }

    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_scr, screen_event_cb, LV_EVENT_CLICKED, NULL);

    s_title = make_label(s_scr, &lv_font_montserrat_36, COLOR_TEXT);
    lv_obj_set_pos(s_title, MARGIN, 14);

    s_clock = make_label(s_scr, &lv_font_montserrat_26, COLOR_SUBTLE);
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -MARGIN, 22);

    for (int i = 0; i < s_page_count; i++) {
        build_page(&s_pages[i]);
    }

    /* App ID panel, in place of the pages until there is a working appID. */
    {
        lv_obj_t *btn, *l;

        s_setup = plain_obj(s_scr);
        lv_obj_set_pos(s_setup, 0, SECTION_Y0);
        lv_obj_set_size(s_setup, SCREEN_W, BODY_BOTTOM - SECTION_Y0);
        lv_obj_add_flag(s_setup, LV_OBJ_FLAG_HIDDEN);

        s_setup_title = make_label(s_setup, &lv_font_montserrat_26, COLOR_TEXT);
        lv_obj_set_width(s_setup_title, SCREEN_W - 2 * MARGIN);
        lv_obj_set_style_text_align(s_setup_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_setup_title, LV_ALIGN_TOP_MID, 0, 70);

        s_setup_body = make_label(s_setup, &lv_font_montserrat_18, COLOR_SUBTLE);
        lv_obj_set_width(s_setup_body, SCREEN_W - 2 * MARGIN);
        lv_obj_set_style_text_align(s_setup_body, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_setup_body, LV_ALIGN_TOP_MID, 0, 124);

        btn = lv_btn_create(s_setup);
        lv_obj_set_size(btn, 240, 64);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 210);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_add_event_cb(btn, setup_btn_cb, LV_EVENT_CLICKED, NULL);
        l = make_label(btn, &lv_font_montserrat_22, COLOR_TEXT);
        lv_label_set_text(l, "Enter App ID");
        lv_obj_center(l);
    }

    s_status = make_label(s_scr, &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, MARGIN, -10);

    s_legend = make_label(s_scr, &lv_font_montserrat_14, COLOR_DIM);
    lv_label_set_text(s_legend, "Gray = scheduled");
    lv_obj_align(s_legend, LV_ALIGN_BOTTOM_RIGHT, -COG_W, -10);

    /* Settings cog in the bottom-right corner (inside the tap-zone border). */
    {
        lv_obj_t *cog = lv_obj_create(s_scr);
        lv_obj_t *icon;

        lv_obj_remove_style_all(cog);
        lv_obj_add_flag(cog, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(cog, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(cog, COG_W, COG_H);
        lv_obj_align(cog, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_add_event_cb(cog, cog_event_cb, LV_EVENT_CLICKED, NULL);

        /* Text color is set on the button so the icon inherits the pressed state. */
        lv_obj_set_style_text_color(cog, lv_color_hex(COLOR_DIM), 0);
        lv_obj_set_style_text_color(cog, lv_color_hex(COLOR_TEXT), LV_STATE_PRESSED);
        icon = lv_label_create(cog);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_22, 0);
        lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
        lv_obj_align(icon, LV_ALIGN_BOTTOM_RIGHT, -MARGIN, -8);
    }

    /* One dot per page, centered, 20 px apart. */
    for (int i = 0; i < s_page_count; i++) {
        s_dots[i] = plain_obj(s_scr);
        lv_obj_set_size(s_dots[i], 10, 10);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_BOTTOM_MID, (2 * i - (s_page_count - 1)) * 10, -14);
        if (s_page_count == 1) {
            lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_timer_create(refresh_timer_cb, REFRESH_MS, NULL);
    s_rotate_timer = lv_timer_create(rotate_timer_cb, trimet_config_get_rotate_sec() * 1000, NULL);

    trimet_settings_init(s_scr);
    /* Also the fallback for the Wi-Fi screen's back button (e.g. first-boot setup). */
    ui_set_screen_last(s_scr);

    render();
    lv_disp_load_scr(s_scr);
    ESP_LOGI(TAG, "%d pages", s_page_count);
}

lv_obj_t *trimet_view_screen(void)
{
    return s_scr;
}

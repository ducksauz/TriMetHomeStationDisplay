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
#include "trimet_format.h"

#define SCREEN_W        480
#define SCREEN_H        480
#define MARGIN          16

#define HEADER_H        64
#define SECTION_Y0      72
#define SECTION_H       182
#define SECTION_GAP     6
#define SECTION_HDR_H   32
#define ROW_H           50
#define BADGE_W         92
#define BADGE_H         38
#define ETA_W           112

#define STOPS_PER_PAGE  2
#define ROWS_PER_STOP   3
#define MAX_MINUTES     90     /* hide arrivals further out than this */
#define REFRESH_MS      1000

#define COLOR_BG        0x0B0E11
#define COLOR_TEXT      0xFFFFFF
#define COLOR_SUBTLE    0xC9D1D9
#define COLOR_DIM       0x8B949E
#define COLOR_WARN      0xF0A030
#define COLOR_ERROR     0xE5534B
#define COLOR_DOT_OFF   0x3A4048

static const char *TAG = "trimet-view";

typedef struct {
    const char    *title;
    trimet_mode_t  mode;
    uint32_t       stops[STOPS_PER_PAGE];
    int            stop_count;
} page_t;

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
    row_t     rows[ROWS_PER_STOP];
} section_t;

static page_t     s_pages[2];
static int        s_page;
static lv_obj_t  *s_scr;
static lv_obj_t  *s_title;
static lv_obj_t  *s_clock;
static lv_obj_t  *s_status;
static lv_obj_t  *s_legend;
static lv_obj_t  *s_dots[2];
static section_t  s_sections[STOPS_PER_PAGE];
static lv_timer_t *s_rotate_timer;
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

static void render_section(section_t *sec, uint32_t loc_id, trimet_mode_t mode,
                           bool have_data, int64_t now_ms, bool *any_scheduled)
{
    const trimet_arrival_t *sel[ROWS_PER_STOP];
    const trimet_location_t *loc = NULL;
    char name[TRIMET_DESC_LEN];
    int n = 0;

    set_hidden(sec->cont, false);
    if (have_data) {
        loc = trimet_find_location(s_data, loc_id);
        n = trimet_select(s_data, loc_id, mode, now_ms, MAX_MINUTES, sel, ROWS_PER_STOP);
    }
    if (have_data) {
        trimet_format_stop_name(s_data, loc_id, name, sizeof(name));
    } else {
        snprintf(name, sizeof(name), "Stop %" PRIu32, loc_id);
    }
    set_text(sec->name, name);
    set_text(sec->dir, loc ? loc->dir : "");

    for (int i = 0; i < ROWS_PER_STOP; i++) {
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
        snprintf(buf, sizeof(buf), "TriMet appID not set");
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
        if (age_s > 2 * CONFIG_TRIMET_POLL_INTERVAL_SEC + 10) {
            color = COLOR_WARN;
        }
    }
    set_text(s_status, buf);
    set_text_color(s_status, color);
}

static void render(void)
{
    trimet_client_status_t st;
    bool have_data = trimet_client_get(s_data, &st);
    const page_t *page = &s_pages[s_page];
    int64_t now_ms = 0;
    bool any_scheduled = false;

    if (have_data) {
        /* Server time now = response time + time elapsed since the fetch. */
        now_ms = s_data->query_time_ms + (esp_timer_get_time() / 1000 - st.last_ok_uptime_ms);
    }

    set_text(s_title, page->title);
    for (int i = 0; i < 2; i++) {
        set_bg_color(s_dots[i], i == s_page ? COLOR_TEXT : COLOR_DOT_OFF);
    }
    for (int i = 0; i < STOPS_PER_PAGE; i++) {
        if (i < page->stop_count) {
            render_section(&s_sections[i], page->stops[i], page->mode, have_data, now_ms, &any_scheduled);
        } else {
            set_hidden(s_sections[i].cont, true);
        }
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

static int other_page(void)
{
    int next = 1 - s_page;
    return s_pages[next].stop_count > 0 ? next : s_page;
}

/* ---- events and timers (run in the LVGL task, lock held) ---- */

static void refresh_timer_cb(lv_timer_t *t)
{
    if (lv_scr_act() == s_scr) {
        render();
    }
}

static void rotate_timer_cb(lv_timer_t *t)
{
    if (lv_scr_act() == s_scr) {
        show_page(other_page());
    }
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
        show_page(other_page());
        lv_timer_reset(s_rotate_timer);   /* give the new page a full interval */
    }
}

/* ---- construction ---- */

static void build_section(section_t *sec, int index)
{
    sec->cont = plain_obj(s_scr);
    lv_obj_set_pos(sec->cont, 0, SECTION_Y0 + index * (SECTION_H + SECTION_GAP));
    lv_obj_set_size(sec->cont, SCREEN_W, SECTION_H);

    sec->name = make_label(sec->cont, &lv_font_montserrat_20, COLOR_SUBTLE);
    lv_label_set_long_mode(sec->name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sec->name, SCREEN_W - 2 * MARGIN - 120);
    lv_obj_set_pos(sec->name, MARGIN, 4);

    sec->dir = make_label(sec->cont, &lv_font_montserrat_16, COLOR_DIM);
    lv_obj_align(sec->dir, LV_ALIGN_TOP_RIGHT, -MARGIN, 8);

    sec->empty = make_label(sec->cont, &lv_font_montserrat_18, COLOR_DIM);
    lv_label_set_text_fmt(sec->empty, "No arrivals in the next %d min", MAX_MINUTES);
    lv_obj_set_pos(sec->empty, MARGIN, SECTION_HDR_H + 14);

    for (int i = 0; i < ROWS_PER_STOP; i++) {
        row_t *r = &sec->rows[i];

        r->row = plain_obj(sec->cont);
        lv_obj_set_pos(r->row, 0, SECTION_HDR_H + i * ROW_H);
        lv_obj_set_size(r->row, SCREEN_W, ROW_H);

        r->badge = plain_obj(r->row);
        lv_obj_set_size(r->badge, BADGE_W, BADGE_H);
        lv_obj_align(r->badge, LV_ALIGN_LEFT_MID, MARGIN, 0);
        lv_obj_set_style_radius(r->badge, 8, 0);
        lv_obj_set_style_bg_opa(r->badge, LV_OPA_COVER, 0);

        r->badge_label = make_label(r->badge, &lv_font_montserrat_20, COLOR_TEXT);
        lv_obj_center(r->badge_label);

        r->dest = make_label(r->row, &lv_font_montserrat_22, COLOR_TEXT);
        lv_label_set_long_mode(r->dest, LV_LABEL_LONG_DOT);
        lv_obj_set_width(r->dest, SCREEN_W - (MARGIN + BADGE_W + 12) - ETA_W - MARGIN);
        lv_obj_align(r->dest, LV_ALIGN_LEFT_MID, MARGIN + BADGE_W + 12, 0);

        r->eta = make_label(r->row, &lv_font_montserrat_28, COLOR_TEXT);
        lv_obj_set_width(r->eta, ETA_W);
        lv_obj_set_style_text_align(r->eta, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(r->eta, LV_ALIGN_RIGHT_MID, -MARGIN, 0);

        lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void init_pages(void)
{
    s_pages[0].title = "Streetcar";
    s_pages[0].mode = TRIMET_MODE_STREETCAR;
    s_pages[0].stop_count = trimet_parse_id_list(CONFIG_TRIMET_STREETCAR_STOP_IDS, s_pages[0].stops, STOPS_PER_PAGE);

    s_pages[1].title = "MAX";
    s_pages[1].mode = TRIMET_MODE_LIGHT_RAIL;
    s_pages[1].stop_count = trimet_parse_id_list(CONFIG_TRIMET_MAX_STOP_IDS, s_pages[1].stops, STOPS_PER_PAGE);

    s_page = s_pages[0].stop_count > 0 ? 0 : 1;
}

void trimet_view_init(void)
{
    s_data = heap_caps_calloc(1, sizeof(*s_data), MALLOC_CAP_SPIRAM);
    if (!s_data) {
        ESP_LOGE(TAG, "out of memory");
        return;
    }
    init_pages();

    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_scr, screen_event_cb, LV_EVENT_CLICKED, NULL);

    s_title = make_label(s_scr, &lv_font_montserrat_36, COLOR_TEXT);
    lv_obj_set_pos(s_title, MARGIN, 14);

    s_clock = make_label(s_scr, &lv_font_montserrat_26, COLOR_SUBTLE);
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -MARGIN, 22);

    for (int i = 0; i < STOPS_PER_PAGE; i++) {
        build_section(&s_sections[i], i);
    }

    s_status = make_label(s_scr, &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, MARGIN, -10);

    s_legend = make_label(s_scr, &lv_font_montserrat_14, COLOR_DIM);
    lv_label_set_text(s_legend, "Gray = scheduled");
    lv_obj_align(s_legend, LV_ALIGN_BOTTOM_RIGHT, -MARGIN, -10);

    for (int i = 0; i < 2; i++) {
        s_dots[i] = plain_obj(s_scr);
        lv_obj_set_size(s_dots[i], 10, 10);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_BOTTOM_MID, i == 0 ? -10 : 10, -14);
    }
    if (s_pages[0].stop_count == 0 || s_pages[1].stop_count == 0) {
        lv_obj_add_flag(s_dots[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dots[1], LV_OBJ_FLAG_HIDDEN);
    }

    lv_timer_create(refresh_timer_cb, REFRESH_MS, NULL);
    s_rotate_timer = lv_timer_create(rotate_timer_cb, CONFIG_TRIMET_PAGE_ROTATE_SEC * 1000, NULL);

    render();
    lv_disp_load_scr(s_scr);
    ESP_LOGI(TAG, "streetcar stops: %d, MAX stops: %d, rotate every %d s",
             s_pages[0].stop_count, s_pages[1].stop_count, CONFIG_TRIMET_PAGE_ROTATE_SEC);
}

lv_obj_t *trimet_view_screen(void)
{
    return s_scr;
}

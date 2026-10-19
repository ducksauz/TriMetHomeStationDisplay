#include "trimet_format.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PAST_GRACE_MS 30000

static const struct {
    const char *name;
    uint32_t    color;
} MAX_LINE_COLORS[] = {
    { "Blue",   0x084C8D },
    { "Green",  0x008852 },
    { "Orange", 0xD05F27 },
    { "Red",    0xD81526 },
    { "Yellow", 0xFFC52F },
};

static void copy_n(char *dst, size_t dst_len, const char *src, size_t n)
{
    if (dst_len == 0) {
        return;
    }
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Case-insensitive search for needle in haystack. */
static const char *find_ci(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, n) == 0) {
            return haystack;
        }
    }
    return NULL;
}

static int ends_with(const char *s, size_t len, const char *suffix)
{
    size_t n = strlen(suffix);
    return len >= n && strncmp(s + len - n, suffix, n) == 0;
}

void trimet_split_sign(const trimet_arrival_t *a,
                       char *badge, size_t badge_len,
                       char *dest, size_t dest_len)
{
    const char *sign = a->short_sign[0] ? a->short_sign : a->full_sign;
    const char *to;
    const char *line = sign;
    size_t line_len;

    to = find_ci(sign, " to ");
    if (!to) {
        snprintf(badge, badge_len, "%u", a->route);
        copy_n(dest, dest_len, sign, strlen(sign));
        return;
    }

    if (strncmp(line, "Portland Streetcar ", 19) == 0) {
        line += 19;
    }
    if (strncmp(line, "MAX ", 4) == 0) {
        line += 4;
    }
    line_len = (size_t)(to - line);
    if (ends_with(line, line_len, " Line")) {
        line_len -= 5;
    } else if (ends_with(line, line_len, " Loop")) {
        line_len -= 5;
    }

    if (line_len == 0) {
        snprintf(badge, badge_len, "%u", a->route);
    } else {
        copy_n(badge, badge_len, line, line_len);
    }
    copy_n(dest, dest_len, to + 4, strlen(to + 4));
}

uint32_t trimet_badge_color(const trimet_arrival_t *a, const char *badge)
{
    if (a->mode == TRIMET_MODE_LIGHT_RAIL) {
        for (size_t i = 0; i < sizeof(MAX_LINE_COLORS) / sizeof(MAX_LINE_COLORS[0]); i++) {
            if (strcmp(badge, MAX_LINE_COLORS[i].name) == 0) {
                return MAX_LINE_COLORS[i].color;
            }
        }
    }
    return a->route_color;
}

int trimet_color_is_light(uint32_t rgb)
{
    unsigned r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    /* Rec. 601 luma; threshold picks black text for yellow, white for the rest. */
    return (299 * r + 587 * g + 114 * b) / 1000 > 160;
}

int trimet_parse_id_list(const char *csv, uint32_t *ids, int max)
{
    int n = 0;

    while (csv && *csv && n < max) {
        char *end;
        unsigned long v;

        while (*csv == ',' || isspace((unsigned char)*csv)) {
            csv++;
        }
        if (!*csv) {
            break;
        }
        v = strtoul(csv, &end, 10);
        if (end == csv) {
            /* Not a number: skip to the next comma. */
            while (*csv && *csv != ',') {
                csv++;
            }
            continue;
        }
        if (v > 0) {
            ids[n++] = (uint32_t)v;
        }
        csv = end;
    }
    return n;
}

int trimet_select(const trimet_arrivals_t *all, uint32_t loc_id, trimet_mode_t mode,
                  int64_t now_ms, int max_minutes,
                  const trimet_arrival_t **out, int max)
{
    int n = 0;
    int64_t latest = now_ms + (int64_t)max_minutes * 60000;

    /* all->arrivals is sorted soonest first. */
    for (int i = 0; i < all->arrival_count && n < max; i++) {
        const trimet_arrival_t *a = &all->arrivals[i];
        if (a->loc_id != loc_id || a->mode != mode) {
            continue;
        }
        if (a->time_ms < now_ms - PAST_GRACE_MS || a->time_ms > latest) {
            continue;
        }
        out[n++] = a;
    }
    return n;
}

void trimet_format_eta(const trimet_arrival_t *a, int64_t now_ms, char *buf, size_t len)
{
    int64_t mins;

    if (a->status == TRIMET_STATUS_CANCELED) {
        snprintf(buf, len, "Canceled");
        return;
    }
    mins = (a->time_ms - now_ms) / 60000;
    if (mins < 1) {
        snprintf(buf, len, "Due");
    } else {
        snprintf(buf, len, "%" PRId64 " min", mins);
    }
}

void trimet_format_stop_name(const trimet_arrivals_t *all, uint32_t loc_id, char *buf, size_t len)
{
    const trimet_location_t *loc = trimet_find_location(all, loc_id);
    size_t n;

    if (!loc || !loc->desc[0]) {
        snprintf(buf, len, "Stop %" PRIu32, loc_id);
        return;
    }
    n = strlen(loc->desc);
    if (ends_with(loc->desc, n, " MAX Stn")) {
        n -= 8;
    } else if (ends_with(loc->desc, n, " MAX Station")) {
        n -= 12;
    }
    copy_n(buf, len, loc->desc, n);
}

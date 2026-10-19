#include "trimet_parse.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define DEFAULT_ROUTE_COLOR 0x808080

/* Copy src into dst, collapsing runs of whitespace and trimming the ends.
 * TriMet signs contain double spaces, e.g. "MAX  Yellow Line". */
static void copy_clean(char *dst, size_t dst_len, const char *src)
{
    size_t n = 0;
    bool pending_space = false;

    if (dst_len == 0) {
        return;
    }
    for (; src && *src && n + 1 < dst_len; src++) {
        if (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
            pending_space = (n > 0);
            continue;
        }
        if (pending_space) {
            if (n + 2 >= dst_len) {
                break;
            }
            dst[n++] = ' ';
            pending_space = false;
        }
        dst[n++] = *src;
    }
    dst[n] = '\0';
}

static const char *get_str(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int64_t get_i64(const cJSON *obj, const char *key, int64_t def)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    /* Epoch-ms values fit exactly in a double. */
    return cJSON_IsNumber(item) ? (int64_t)item->valuedouble : def;
}

static bool get_bool(const cJSON *obj, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key));
}

static trimet_status_t parse_status(const char *s)
{
    if (!s) {
        return TRIMET_STATUS_UNKNOWN;
    }
    if (strcmp(s, "estimated") == 0) {
        return TRIMET_STATUS_ESTIMATED;
    }
    if (strcmp(s, "scheduled") == 0) {
        return TRIMET_STATUS_SCHEDULED;
    }
    if (strcmp(s, "delayed") == 0) {
        return TRIMET_STATUS_DELAYED;
    }
    if (strcmp(s, "canceled") == 0 || strcmp(s, "cancelled") == 0) {
        return TRIMET_STATUS_CANCELED;
    }
    return TRIMET_STATUS_UNKNOWN;
}

static uint32_t parse_color(const char *s)
{
    char *end = NULL;
    unsigned long v;

    if (!s || !*s) {
        return DEFAULT_ROUTE_COLOR;
    }
    if (*s == '#') {
        s++;
    }
    v = strtoul(s, &end, 16);
    return (end && *end == '\0' && v <= 0xFFFFFF) ? (uint32_t)v : DEFAULT_ROUTE_COLOR;
}

static void parse_arrival(const cJSON *a, trimet_arrival_t *out)
{
    memset(out, 0, sizeof(*out));
    out->loc_id       = (uint32_t)get_i64(a, "locid", 0);
    out->route        = (uint16_t)get_i64(a, "route", 0);
    out->dir          = (uint8_t)get_i64(a, "dir", 0);
    out->streetcar    = get_bool(a, "streetCar");
    out->status       = parse_status(get_str(a, "status"));
    out->scheduled_ms = get_i64(a, "scheduled", 0);
    /* "estimated" is absent for schedule-only arrivals. */
    out->time_ms      = get_i64(a, "estimated", out->scheduled_ms);
    out->route_color  = parse_color(get_str(a, "routeColor"));
    copy_clean(out->short_sign, sizeof(out->short_sign), get_str(a, "shortSign"));
    copy_clean(out->full_sign, sizeof(out->full_sign), get_str(a, "fullSign"));
    copy_clean(out->reason, sizeof(out->reason), get_str(a, "reason"));
}

static int cmp_arrival_time(const void *pa, const void *pb)
{
    const trimet_arrival_t *a = pa, *b = pb;
    return (a->time_ms > b->time_ms) - (a->time_ms < b->time_ms);
}

trimet_parse_result_t trimet_parse_arrivals(const char *json, size_t len,
                                            trimet_arrivals_t *out,
                                            char *err, size_t err_len)
{
    cJSON *root;
    const cJSON *rs, *error, *locs, *arrs, *item;
    trimet_parse_result_t result = TRIMET_PARSE_OK;

    memset(out, 0, sizeof(*out));
    if (err && err_len) {
        err[0] = '\0';
    }

    root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return TRIMET_PARSE_BAD_JSON;
    }
    rs = cJSON_GetObjectItemCaseSensitive(root, "resultSet");
    if (!cJSON_IsObject(rs)) {
        result = TRIMET_PARSE_BAD_JSON;
        goto done;
    }

    error = cJSON_GetObjectItemCaseSensitive(rs, "error");
    if (error) {
        if (err && err_len) {
            const char *msg = cJSON_IsObject(error) ? get_str(error, "content")
                                                    : (cJSON_IsString(error) ? error->valuestring : NULL);
            copy_clean(err, err_len, msg ? msg : "unknown API error");
        }
        result = TRIMET_PARSE_API_ERROR;
        goto done;
    }

    out->query_time_ms = get_i64(rs, "queryTime", 0);

    locs = cJSON_GetObjectItemCaseSensitive(rs, "location");
    cJSON_ArrayForEach(item, locs) {
        trimet_location_t *loc;
        if (out->location_count >= TRIMET_MAX_LOCATIONS) {
            break;
        }
        loc = &out->locations[out->location_count++];
        loc->id = (uint32_t)get_i64(item, "id", 0);
        copy_clean(loc->desc, sizeof(loc->desc), get_str(item, "desc"));
        copy_clean(loc->dir, sizeof(loc->dir), get_str(item, "dir"));
    }

    arrs = cJSON_GetObjectItemCaseSensitive(rs, "arrival");
    cJSON_ArrayForEach(item, arrs) {
        trimet_arrival_t a;
        parse_arrival(item, &a);
        if (out->arrival_count < TRIMET_MAX_ARRIVALS) {
            out->arrivals[out->arrival_count++] = a;
        } else {
            /* Full: replace the latest arrival if this one is sooner. */
            int latest = 0;
            for (int i = 1; i < out->arrival_count; i++) {
                if (out->arrivals[i].time_ms > out->arrivals[latest].time_ms) {
                    latest = i;
                }
            }
            if (a.time_ms < out->arrivals[latest].time_ms) {
                out->arrivals[latest] = a;
            }
        }
    }
    qsort(out->arrivals, out->arrival_count, sizeof(out->arrivals[0]), cmp_arrival_time);

done:
    cJSON_Delete(root);
    return result;
}

const trimet_location_t *trimet_find_location(const trimet_arrivals_t *a, uint32_t loc_id)
{
    for (int i = 0; i < a->location_count; i++) {
        if (a->locations[i].id == loc_id) {
            return &a->locations[i];
        }
    }
    return NULL;
}

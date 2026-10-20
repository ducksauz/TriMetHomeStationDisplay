/* Host unit tests for main/trimet/trimet_parse.c and trimet_format.c. Run: make -C test/host */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trimet_format.h"
#include "trimet_parse.h"

static int failures, checks;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(a, b) do { \
    checks++; \
    if (strcmp((a), (b)) != 0) { failures++; fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } \
} while (0)

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(n + 1);
    *len = fread(buf, 1, n, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

static trimet_arrivals_t out;  /* large; keep off the stack */

static void test_fixture(void)
{
    size_t len;
    char *json = read_file("fixtures/arrivals_4stops.json", &len);
    char err[TRIMET_ERROR_LEN];
    const trimet_location_t *loc;
    int scheduled_seen = 0;

    CHECK(trimet_parse_arrivals(json, len, &out, err, sizeof(err)) == TRIMET_PARSE_OK);
    CHECK(out.query_time_ms == 1792383460355LL);
    CHECK(out.location_count == 4);
    CHECK(out.arrival_count == 15);

    loc = trimet_find_location(&out, 7601);
    CHECK(loc != NULL);
    if (loc) {
        CHECK_STR(loc->desc, "Union Station/NW 5th & Glisan MAX Stn");
        CHECK_STR(loc->dir, "Southbound");
    }
    CHECK(trimet_find_location(&out, 12345) == NULL);

    /* Sorted soonest first. */
    for (int i = 1; i < out.arrival_count; i++) {
        CHECK(out.arrivals[i - 1].time_ms <= out.arrivals[i].time_ms);
    }

    /* Soonest is the B Loop at NW 11th & Glisan. */
    CHECK(out.arrivals[0].loc_id == 10754);
    CHECK(out.arrivals[0].route == 195);
    CHECK(out.arrivals[0].streetcar);
    CHECK(out.arrivals[0].status == TRIMET_STATUS_ESTIMATED);
    CHECK(out.arrivals[0].route_color == 0x4650BE);
    CHECK(out.arrivals[0].time_ms == 1792383535787LL);

    for (int i = 0; i < out.arrival_count; i++) {
        const trimet_arrival_t *a = &out.arrivals[i];
        if (a->status == TRIMET_STATUS_SCHEDULED) {
            scheduled_seen++;
            /* No "estimated" in the JSON: fall back to scheduled. */
            CHECK(a->time_ms == a->scheduled_ms);
            CHECK(a->time_ms > 0);
        }
        if (a->streetcar) {
            CHECK(a->mode == TRIMET_MODE_STREETCAR);
        } else if (a->route == 291 || a->route == 293) {
            CHECK(a->mode == TRIMET_MODE_BUS);
        } else {
            CHECK(a->mode == TRIMET_MODE_LIGHT_RAIL);
        }
        if (a->route == 190 && a->loc_id == 7763) {
            CHECK(!a->streetcar);
            CHECK(a->route_color == 0xFFC52F);
            /* "MAX  Yellow..." has a double space in the feed. */
            CHECK_STR(a->full_sign, "MAX Yellow Line to Expo Center");
        }
    }
    CHECK(scheduled_seen == 4);
    free(json);
}

static void test_api_error(void)
{
    const char *json = "{\"resultSet\":{\"error\":{\"content\":\"Location id not found 99999999\"}}}";
    char err[TRIMET_ERROR_LEN];

    CHECK(trimet_parse_arrivals(json, strlen(json), &out, err, sizeof(err)) == TRIMET_PARSE_API_ERROR);
    CHECK_STR(err, "Location id not found 99999999");
    CHECK(out.arrival_count == 0);
}

static void test_bad_json(void)
{
    const char *cases[] = {
        "A valid appID is required to access this resource.",
        "{}",
        "{\"resultSet\":",
        "",
    };
    char err[TRIMET_ERROR_LEN];

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(trimet_parse_arrivals(cases[i], strlen(cases[i]), &out, err, sizeof(err)) == TRIMET_PARSE_BAD_JSON);
    }
}

static void test_empty_result(void)
{
    const char *json = "{\"resultSet\":{\"queryTime\":1,\"location\":[{\"id\":1,\"desc\":\"X\",\"dir\":\"North\"}]}}";

    CHECK(trimet_parse_arrivals(json, strlen(json), &out, NULL, 0) == TRIMET_PARSE_OK);
    CHECK(out.location_count == 1);
    CHECK(out.arrival_count == 0);
}

/* More arrivals than fit: keep the soonest TRIMET_MAX_ARRIVALS, sorted. */
static void test_overflow_keeps_soonest(void)
{
    const int n = TRIMET_MAX_ARRIVALS + 10;
    size_t cap = 64 + n * 128, len = 0;
    char *json = malloc(cap);

    len += snprintf(json + len, cap - len, "{\"resultSet\":{\"queryTime\":0,\"arrival\":[");
    for (int i = 0; i < n; i++) {
        /* Descending times, so the soonest arrive last. */
        len += snprintf(json + len, cap - len,
                        "%s{\"locid\":1,\"route\":%d,\"status\":\"estimated\",\"estimated\":%d,\"scheduled\":%d}",
                        i ? "," : "", i, (n - i) * 1000, (n - i) * 1000);
    }
    len += snprintf(json + len, cap - len, "]}}");

    CHECK(trimet_parse_arrivals(json, len, &out, NULL, 0) == TRIMET_PARSE_OK);
    CHECK(out.arrival_count == TRIMET_MAX_ARRIVALS);
    CHECK(out.arrivals[0].time_ms == 1000);
    CHECK(out.arrivals[TRIMET_MAX_ARRIVALS - 1].time_ms == TRIMET_MAX_ARRIVALS * 1000);
    free(json);
}

static void test_field_edge_cases(void)
{
    char long_sign[300];
    char json[600];

    memset(long_sign, 'A', sizeof(long_sign) - 1);
    long_sign[sizeof(long_sign) - 1] = '\0';
    snprintf(json, sizeof(json),
             "{\"resultSet\":{\"arrival\":[{\"locid\":5,\"status\":\"canceled\",\"reason\":\"  Traffic   issue \","
             "\"routeColor\":\"zzz\",\"scheduled\":10,\"shortSign\":\"%s\"}]}}", long_sign);

    CHECK(trimet_parse_arrivals(json, strlen(json), &out, NULL, 0) == TRIMET_PARSE_OK);
    CHECK(out.arrival_count == 1);
    CHECK(out.arrivals[0].status == TRIMET_STATUS_CANCELED);
    CHECK_STR(out.arrivals[0].reason, "Traffic issue");
    CHECK(out.arrivals[0].route_color == 0x808080);  /* invalid color -> default */
    CHECK(strlen(out.arrivals[0].short_sign) == TRIMET_SIGN_LEN - 1);  /* truncated, terminated */
    CHECK(out.arrivals[0].full_sign[0] == '\0');      /* missing field */
    CHECK(out.arrivals[0].mode == TRIMET_MODE_OTHER);  /* no routeSubType */
}

static trimet_arrival_t make_arrival(const char *short_sign, uint16_t route, trimet_mode_t mode)
{
    trimet_arrival_t a;
    memset(&a, 0, sizeof(a));
    snprintf(a.short_sign, sizeof(a.short_sign), "%s", short_sign);
    a.route = route;
    a.mode = mode;
    a.route_color = 0x123456;
    return a;
}

static void check_split(const char *sign, uint16_t route, trimet_mode_t mode,
                        const char *want_badge, const char *want_dest)
{
    trimet_arrival_t a = make_arrival(sign, route, mode);
    char badge[TRIMET_BADGE_LEN], dest[TRIMET_SIGN_LEN];

    trimet_split_sign(&a, badge, sizeof(badge), dest, sizeof(dest));
    CHECK_STR(badge, want_badge);
    CHECK_STR(dest, want_dest);
}

static void test_split_sign(void)
{
    check_split("Portland Streetcar NS Line to NW 23rd Ave", 193, TRIMET_MODE_STREETCAR, "NS", "NW 23rd Ave");
    check_split("Portland Streetcar A Loop to Lloyd via Pearl", 194, TRIMET_MODE_STREETCAR, "A", "Lloyd via Pearl");
    check_split("Portland Streetcar B Loop to Lloyd via OMSI", 195, TRIMET_MODE_STREETCAR, "B", "Lloyd via OMSI");
    check_split("Yellow Line to Expo Ctr", 190, TRIMET_MODE_LIGHT_RAIL, "Yellow", "Expo Ctr");
    check_split("Orange Line to E 197th", 190, TRIMET_MODE_LIGHT_RAIL, "Orange", "E 197th");
    check_split("Yellow Line to City Ctr/PSU", 190, TRIMET_MODE_LIGHT_RAIL, "Yellow", "City Ctr/PSU");
    check_split("291 To Milwaukie", 291, TRIMET_MODE_BUS, "291", "Milwaukie");
    check_split("No destination here", 77, TRIMET_MODE_BUS, "77", "No destination here");
    check_split("", 5, TRIMET_MODE_OTHER, "5", "");
    /* Badge longer than the buffer is truncated, not overflowed. */
    check_split("Extraordinarily Long Line to Somewhere", 1, TRIMET_MODE_OTHER, "Extraordina", "Somewhere");
}

static void test_badge_color(void)
{
    trimet_arrival_t max = make_arrival("Orange Line to E 197th", 190, TRIMET_MODE_LIGHT_RAIL);
    trimet_arrival_t sc  = make_arrival("Portland Streetcar NS Line to NW 23rd Ave", 193, TRIMET_MODE_STREETCAR);

    /* Route 190 is Yellow, but the sign says Orange: color follows the sign. */
    max.route_color = 0xFFC52F;
    CHECK(trimet_badge_color(&max, "Orange") == 0xD05F27);
    CHECK(trimet_badge_color(&max, "Purple") == 0xFFC52F);
    sc.route_color = 0x72A130;
    CHECK(trimet_badge_color(&sc, "NS") == 0x72A130);

    CHECK(trimet_color_is_light(0xFFC52F));   /* Yellow -> black text */
    CHECK(!trimet_color_is_light(0xD05F27));  /* Orange */
    CHECK(!trimet_color_is_light(0x72A130));  /* NS green */
    CHECK(!trimet_color_is_light(0x4650BE));  /* B Loop blue */
    CHECK(!trimet_color_is_light(0xD91965));  /* A Loop magenta */
}

static void test_parse_id_list(void)
{
    uint32_t ids[4];

    CHECK(trimet_parse_id_list("10772,10754", ids, 4) == 2);
    CHECK(ids[0] == 10772 && ids[1] == 10754);
    CHECK(trimet_parse_id_list(" 7601 , 7763 ,", ids, 4) == 2);
    CHECK(ids[0] == 7601 && ids[1] == 7763);
    CHECK(trimet_parse_id_list("1,2,3,4,5,6", ids, 4) == 4);
    CHECK(trimet_parse_id_list("abc,42", ids, 4) == 1);
    CHECK(ids[0] == 42);
    CHECK(trimet_parse_id_list("", ids, 4) == 0);
    CHECK(trimet_parse_id_list(NULL, ids, 4) == 0);
}

static void test_select_and_eta(void)
{
    size_t len;
    char *json = read_file("fixtures/arrivals_4stops.json", &len);
    const trimet_arrival_t *sel[8];
    char buf[32];
    int n;
    int64_t now;

    CHECK(trimet_parse_arrivals(json, len, &out, NULL, 0) == TRIMET_PARSE_OK);
    now = out.query_time_ms;

    /* 10754 has 4 streetcar arrivals; ask for 3. */
    n = trimet_select(&out, 10754, TRIMET_MODE_STREETCAR, now, 90, sel, 3);
    CHECK(n == 3);
    for (int i = 0; i < n; i++) {
        CHECK(sel[i]->loc_id == 10754);
        CHECK(i == 0 || sel[i - 1]->time_ms <= sel[i]->time_ms);
    }

    /* 7601 light rail only: the 291/293 shuttle buses are excluded, and the
     * Yellow Line trip 8+ hours out is beyond the 90 minute window. */
    n = trimet_select(&out, 7601, TRIMET_MODE_LIGHT_RAIL, now, 90, sel, 8);
    CHECK(n == 2);
    for (int i = 0; i < n; i++) {
        CHECK(sel[i]->route == 290);
    }
    CHECK(trimet_select(&out, 7601, TRIMET_MODE_BUS, now, 24 * 60, sel, 8) == 2);

    /* Wrong mode for the stop: nothing. */
    CHECK(trimet_select(&out, 10772, TRIMET_MODE_LIGHT_RAIL, now, 90, sel, 8) == 0);

    /* Arrivals more than 30 s in the past drop off. */
    n = trimet_select(&out, 10754, TRIMET_MODE_STREETCAR, out.arrivals[0].time_ms + 31000, 90, sel, 8);
    CHECK(n == 3);

    /* ETA formatting. */
    {
        trimet_arrival_t a = make_arrival("x to y", 1, TRIMET_MODE_STREETCAR);
        a.time_ms = 1000000;
        trimet_format_eta(&a, 1000000 + 10000, buf, sizeof(buf));   /* 10 s ago */
        CHECK_STR(buf, "Due");
        trimet_format_eta(&a, 1000000 - 59999, buf, sizeof(buf));   /* <1 min */
        CHECK_STR(buf, "Due");
        trimet_format_eta(&a, 1000000 - 60000, buf, sizeof(buf));
        CHECK_STR(buf, "1 min");
        trimet_format_eta(&a, 1000000 - 12 * 60000 - 59000, buf, sizeof(buf));
        CHECK_STR(buf, "12 min");
        a.status = TRIMET_STATUS_CANCELED;
        trimet_format_eta(&a, 0, buf, sizeof(buf));
        CHECK_STR(buf, "Canceled");
    }

    /* Stop names. */
    trimet_format_stop_name(&out, 7601, buf, sizeof(buf));
    CHECK_STR(buf, "Union Station/NW 5th & Glisan");
    trimet_format_stop_name(&out, 10772, buf, sizeof(buf));
    CHECK_STR(buf, "NW 10th & Glisan");
    trimet_format_stop_name(&out, 999, buf, sizeof(buf));
    CHECK_STR(buf, "Stop 999");

    free(json);
}

static void test_time_window(void)
{
    char buf[16];

    /* 10 PM - 6 AM wraps past midnight. */
    CHECK(trimet_time_in_window(22 * 60, 22 * 60, 6 * 60));
    CHECK(trimet_time_in_window(23 * 60 + 59, 22 * 60, 6 * 60));
    CHECK(trimet_time_in_window(0, 22 * 60, 6 * 60));
    CHECK(trimet_time_in_window(5 * 60 + 59, 22 * 60, 6 * 60));
    CHECK(!trimet_time_in_window(6 * 60, 22 * 60, 6 * 60));
    CHECK(!trimet_time_in_window(12 * 60, 22 * 60, 6 * 60));
    CHECK(!trimet_time_in_window(21 * 60 + 59, 22 * 60, 6 * 60));

    /* Same-day window. */
    CHECK(trimet_time_in_window(13 * 60, 12 * 60, 14 * 60));
    CHECK(!trimet_time_in_window(14 * 60, 12 * 60, 14 * 60));
    CHECK(!trimet_time_in_window(11 * 60, 12 * 60, 14 * 60));

    /* Empty window. */
    CHECK(!trimet_time_in_window(0, 0, 0));
    CHECK(!trimet_time_in_window(600, 600, 600));

    trimet_format_time_of_day(22 * 60, buf, sizeof(buf));
    CHECK_STR(buf, "10:00 PM");
    trimet_format_time_of_day(6 * 60, buf, sizeof(buf));
    CHECK_STR(buf, "6:00 AM");
    trimet_format_time_of_day(0, buf, sizeof(buf));
    CHECK_STR(buf, "12:00 AM");
    trimet_format_time_of_day(12 * 60 + 45, buf, sizeof(buf));
    CHECK_STR(buf, "12:45 PM");
    trimet_format_time_of_day(23 * 60 + 15, buf, sizeof(buf));
    CHECK_STR(buf, "11:15 PM");
}

static void test_version(void)
{
    /* Local builds are versioned from git describe; even a hash that starts
     * with digits must not parse as a version. */
    CHECK(trimet_version_is_newer("v1.1.0", "6b546e6-dirty"));
    CHECK(trimet_version_is_newer("v1.1.0", "7be3db6"));
    CHECK(trimet_version_is_newer("v1.1.0", "123.4.5"));     /* no "v" */
    CHECK(trimet_version_is_newer("v1.1.0", ""));
    CHECK(trimet_version_is_newer("v1.1.1", "v1.1.0"));
    CHECK(trimet_version_is_newer("v1.2.0", "v1.1.9"));
    CHECK(trimet_version_is_newer("v2.0.0", "v1.10.0"));
    CHECK(trimet_version_is_newer("v1.10.0", "v1.9.0"));     /* numeric, not text */
    CHECK(!trimet_version_is_newer("v1.1.0", "v1.1.0"));
    CHECK(!trimet_version_is_newer("v1.0.9", "v1.1.0"));
    /* git describe after a tag: "v1.1.0-3-gabc1234" is the same release line. */
    CHECK(!trimet_version_is_newer("v1.1.0", "v1.1.0-3-gabc1234"));
    CHECK(trimet_version_is_newer("v1.1.1", "v1.1.0-3-gabc1234"));
    /* A bad version file never counts as newer. */
    CHECK(!trimet_version_is_newer("garbage", "6b546e6-dirty"));
    CHECK(!trimet_version_is_newer("v1.1", "6b546e6-dirty"));

    CHECK(trimet_version_valid("v1.1.0"));
    CHECK(trimet_version_valid("V0.0.1"));
    CHECK(!trimet_version_valid("v0.0.0"));
    CHECK(!trimet_version_valid("6b546e6-dirty"));
    CHECK(!trimet_version_valid("v1.1"));
    CHECK(!trimet_version_valid("v1.1.0x"));
    CHECK(!trimet_version_valid("v1..0"));
}

int main(void)
{
    test_fixture();
    test_api_error();
    test_bad_json();
    test_empty_result();
    test_overflow_keeps_soonest();
    test_field_edge_cases();
    test_split_sign();
    test_badge_color();
    test_parse_id_list();
    test_select_and_eta();
    test_time_window();
    test_version();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

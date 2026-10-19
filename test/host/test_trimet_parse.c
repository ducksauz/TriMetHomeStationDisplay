/* Host unit tests for main/trimet/trimet_parse.c. Run: make -C test/host */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
}

int main(void)
{
    test_fixture();
    test_api_error();
    test_bad_json();
    test_empty_result();
    test_overflow_keeps_soonest();
    test_field_edge_cases();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

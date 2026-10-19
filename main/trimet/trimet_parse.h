/*
 * TriMet Arrivals V2 response parser.
 *
 * Pure C + cJSON with no ESP-IDF dependencies, so it can be unit tested on
 * the host (see test/host).
 */
#ifndef TRIMET_PARSE_H
#define TRIMET_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRIMET_MAX_ARRIVALS   32
#define TRIMET_MAX_LOCATIONS  8
#define TRIMET_SIGN_LEN       72
#define TRIMET_DESC_LEN       48
#define TRIMET_DIR_LEN        16
#define TRIMET_REASON_LEN     48
#define TRIMET_ERROR_LEN      96

typedef enum {
    TRIMET_STATUS_UNKNOWN = 0,
    TRIMET_STATUS_ESTIMATED,   /* real-time prediction */
    TRIMET_STATUS_SCHEDULED,   /* schedule only, no live vehicle data */
    TRIMET_STATUS_DELAYED,
    TRIMET_STATUS_CANCELED,
} trimet_status_t;

typedef struct {
    uint32_t        loc_id;
    uint16_t        route;
    uint8_t         dir;
    bool            streetcar;
    trimet_status_t status;
    int64_t         time_ms;       /* best arrival time: estimated if present, else scheduled (epoch ms) */
    int64_t         scheduled_ms;  /* epoch ms */
    uint32_t        route_color;   /* 0xRRGGBB */
    char            short_sign[TRIMET_SIGN_LEN];
    char            full_sign[TRIMET_SIGN_LEN];
    char            reason[TRIMET_REASON_LEN];  /* set when delayed/canceled */
} trimet_arrival_t;

typedef struct {
    uint32_t id;
    char     desc[TRIMET_DESC_LEN];
    char     dir[TRIMET_DIR_LEN];
} trimet_location_t;

typedef struct {
    int64_t           query_time_ms;   /* server time of the response (epoch ms) */
    int               location_count;
    trimet_location_t locations[TRIMET_MAX_LOCATIONS];
    int               arrival_count;   /* sorted by time_ms, soonest first */
    trimet_arrival_t  arrivals[TRIMET_MAX_ARRIVALS];
} trimet_arrivals_t;

typedef enum {
    TRIMET_PARSE_OK = 0,
    TRIMET_PARSE_BAD_JSON,       /* not JSON, or no resultSet */
    TRIMET_PARSE_API_ERROR,      /* resultSet.error present; message in err */
} trimet_parse_result_t;

/*
 * Parse an Arrivals V2 JSON response into *out. If the response holds more
 * than TRIMET_MAX_ARRIVALS arrivals, the soonest ones are kept. On
 * TRIMET_PARSE_API_ERROR the API's message is copied into err (may be NULL).
 */
trimet_parse_result_t trimet_parse_arrivals(const char *json, size_t len,
                                            trimet_arrivals_t *out,
                                            char *err, size_t err_len);

/* Location for a stop id, or NULL if not in the response. */
const trimet_location_t *trimet_find_location(const trimet_arrivals_t *a, uint32_t loc_id);

#ifdef __cplusplus
}
#endif

#endif

/*
 * Display helpers for TriMet arrivals: sign splitting, line colors, arrival
 * selection and time formatting. No LVGL or ESP-IDF dependencies, so these are
 * unit tested on the host (see test/host).
 */
#ifndef TRIMET_FORMAT_H
#define TRIMET_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "trimet_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRIMET_BADGE_LEN 12

/*
 * Split a sign into a short line badge and a destination:
 *   "Portland Streetcar NS Line to NW 23rd Ave" -> "NS",     "NW 23rd Ave"
 *   "Yellow Line to Expo Ctr"                   -> "Yellow", "Expo Ctr"
 *   "291 To Milwaukie"                          -> "291",    "Milwaukie"
 * Falls back to the route number and the whole sign. Uses the sign rather than
 * the route number because interlined MAX trips can carry a different line's
 * sign (route 190 signed "Orange Line").
 */
void trimet_split_sign(const trimet_arrival_t *a,
                       char *badge, size_t badge_len,
                       char *dest, size_t dest_len);

/* Badge color: the MAX line color for a MAX line name, else the API's routeColor. */
uint32_t trimet_badge_color(const trimet_arrival_t *a, const char *badge);

/* True if black text reads better than white on this background color. */
int trimet_color_is_light(uint32_t rgb);

/* Parse "10772,10754" into ids. Returns the count (at most max). */
int trimet_parse_id_list(const char *csv, uint32_t *ids, int max);

/*
 * Select up to max arrivals at loc_id of the given mode, soonest first,
 * skipping ones more than 30 s in the past or more than max_minutes ahead.
 * now_ms is the current time on the server's clock. Returns the count.
 */
int trimet_select(const trimet_arrivals_t *all, uint32_t loc_id, trimet_mode_t mode,
                  int64_t now_ms, int max_minutes,
                  const trimet_arrival_t **out, int max);

/* "Due", "1 min", "12 min"; "Canceled" for canceled arrivals. */
void trimet_format_eta(const trimet_arrival_t *a, int64_t now_ms, char *buf, size_t len);

/* Stop name for display, with a trailing " MAX Stn" removed (the page title
 * already says MAX). "Stop 12345" if the stop is not in the response.
 * The built-in LVGL fonts are ASCII only, so keep output ASCII. */
void trimet_format_stop_name(const trimet_arrivals_t *all, uint32_t loc_id, char *buf, size_t len);

/*
 * True if now_min (minutes since local midnight) is in [start_min, end_min).
 * The window may wrap past midnight (e.g. 22:00-06:00). start == end is an
 * empty window.
 */
bool trimet_time_in_window(int now_min, int start_min, int end_min);

/* Minutes since midnight -> "10:00 PM". */
void trimet_format_time_of_day(int minutes, char *buf, size_t len);

/*
 * True if release version latest ("v1.2.0") is newer than running. Only a
 * full "vX.Y.Z" (optionally followed by "-...") counts as a version; anything
 * else, like a local build's "6b546e6-dirty", is treated as 0.0.0, so every
 * release is newer than it.
 */
bool trimet_version_is_newer(const char *latest, const char *running);

/* True if v is a full "vX.Y.Z" version as above. */
bool trimet_version_valid(const char *v);

#ifdef __cplusplus
}
#endif

#endif

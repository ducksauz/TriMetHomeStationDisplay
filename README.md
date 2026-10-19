# TriMet Home Station Display

A small always-on display that shows live arrival times for the Portland Streetcar and MAX light rail at stops in my neighborhood (Pearl District / Old Town, near Union Station). It uses the TriMet Arrivals V2 web service.

## Hardware

| Item | Detail |
|------|--------|
| Device | Seeed Studio SenseCAP Indicator D1S |
| MCU | ESP32-S3 (an RP2040 co-processor handles the onboard sensors, which this project doesn't use) |
| Display | 4" 480×480 RGB LCD with capacitive touch |
| Connectivity | Wi-Fi (2.4 GHz) |

## Software stack

- **Framework:** [ESP-IDF](https://docs.espressif.com/projects/esp-idf/)
- **Graphics:** [LVGL](https://docs.lvgl.io/master/intro/index.html)
- **Starting point:** [Seeed-Solution/indicator-esp-idf-template](https://github.com/Seeed-Solution/indicator-esp-idf-template)
- **Data source:** [TriMet Web Services](https://developer.trimet.org/ws_docs/), mainly [Arrivals V2](https://developer.trimet.org/ws_docs/arrivals2_ws.shtml)

## Stops

Stop IDs and route lists come from the TriMet GTFS feed (`stops.txt`, `stop_times.txt`; feed dated 2026-10-07).

| Stop ID | Stop | Direction | Mode | Routes |
|---------|------|-----------|------|--------|
| 10772 | NW 10th & Glisan | Northbound | Streetcar | NS Line, A Loop |
| 10754 | NW 11th & Glisan | Southbound | Streetcar | NS Line, B Loop |
| 7601 | Union Station/NW 5th & Glisan MAX Stn | Southbound | MAX | Yellow, Orange |
| 7763 | Union Station/NW 6th & Hoyt MAX Stn | Northbound | MAX | Yellow |

The MAX stops are also served by shuttle buses 291 (Orange) and 293 (Yellow). These appear to be service-disruption shuttles.

All four stops fit in a single request: `locIDs=10772,10754,7601,7763`.

## TriMet Arrivals V2 API

**Endpoint**

```
GET https://developer.trimet.org/ws/v2/arrivals?locIDs=<id1,id2,...>&appID=<APP_ID>
```

**Request parameters**

| Param | Required | Notes |
|-------|----------|-------|
| `locIDs` | yes | Comma-separated stop IDs (up to 128). These are the GTFS `stop_id`s. |
| `appID` | yes | The developer key from registering at developer.trimet.org |
| `json` | no | Defaults to `true`; `false` returns XML |
| `minutes` | no | Look-ahead window. Defaults to 20, max 60. |
| `arrivals` | no | Minimum arrivals per route/direction. Defaults to 2. |
| `showPosition` | no | Includes vehicle `blockPosition` data. Defaults to `false`. |

**Response fields this project uses** (`resultSet.arrival[]`)

| Field | Use |
|-------|-----|
| `estimated` | Predicted arrival time (Unix epoch **ms**). May be missing when only scheduled data exists. |
| `scheduled` | Scheduled time (epoch ms). For streetcar it is the same as `estimated`. |
| `status` | One of `estimated`, `scheduled`, `delayed`, `canceled` |
| `shortSign` / `fullSign` | Destination text for display |
| `route`, `dir` | Route and direction for grouping |
| `locid` | The stop that the arrival belongs to |
| `routeColor` | Hex color for the route badge |
| `streetCar` | Marks streetcar results (streetcar data is included by default) |
| `reason` | Explanation when the arrival is delayed or canceled |

`resultSet.location[]` gives each stop's `id`, `desc`, `dir`, `lat` and `lng`. `resultSet.queryTime` is the server time in epoch ms, which can serve as a clock fallback.

**Notes**
- Arrival times are absolute, so the device needs accurate wall-clock time (SNTP). Alternatively, compute minutes-away from `queryTime`.
- HTTPS requires the ESP-IDF certificate bundle (`esp_crt_bundle`).
- The docs don't give a rate limit. We should poll politely, about every 30–60 s.
- The `appID` is a secret. Keep it out of source control (see Configuration).

## Configuration

Wi-Fi credentials, the TriMet `appID` and the stop IDs will live in a local, git-ignored config. That will be either a `sdkconfig` set through `idf.py menuconfig` or a `secrets.h`. The exact mechanism will be settled during setup.

## Build & flash

Exact steps will be filled in once the template is imported. The expected flow:

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py menuconfig        # Wi-Fi, appID, stop IDs
idf.py build flash monitor
```

## Project status

Planning. See [TODO.md](TODO.md).

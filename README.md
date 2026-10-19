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
- **Graphics:** [LVGL 8.3](https://docs.lvgl.io/8.3/) (bundled in `components/lvgl`; pinned to v8.3, do not use the v9 docs/APIs)
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

**Wi-Fi** is set up on the device itself, using the template's touchscreen Wi-Fi setup screen. Credentials are stored in NVS and survive reflashing.

**TriMet settings** are set in `idf.py menuconfig` → **TriMet Station Display** (`main/Kconfig.projbuild`):

| Option | Default | Notes |
|--------|---------|-------|
| `CONFIG_TRIMET_APP_ID` | *(empty)* | Your TriMet appID. **Secret.** |
| `CONFIG_TRIMET_STOP_IDS` | `10772,10754,7601,7763` | Sent as `locIDs` |
| `CONFIG_TRIMET_POLL_INTERVAL_SEC` | `30` | 15–300 s |

Values are saved in `sdkconfig`, which is git-ignored. Never put the appID in `sdkconfig.defaults`, because that file is committed. At boot the firmware logs whether the appID is set, without printing the appID itself.

## Code layout

| Path | Purpose |
|------|---------|
| `main/trimet/trimet_parse.{c,h}` | Arrivals V2 JSON → `trimet_arrivals_t` (pure C + cJSON, host-testable) |
| `main/trimet/trimet_client.{c,h}` | HTTPS fetch + polling task; `trimet_client_get()` returns the last good snapshot and status |
| `main/model`, `main/view`, `main/ui` | From the Seeed template (Wi-Fi, time, display, demo UI) |
| `test/host/` | Host unit tests for the parser, run against a captured live response |

**Notes on the live API data:**
- `estimated` is missing for schedule-only arrivals. The parser falls back to `scheduled`.
- Signs contain double spaces (`"MAX  Yellow Line"`). The parser collapses them.
- The `route` number doesn't always match the sign. Interlined MAX trips can show route 190 with an Orange Line sign, so the UI should label arrivals by their sign text.
- A bad appID returns **HTTP 403** in plain text. An unknown stop returns HTTP 200 with `resultSet.error.content`.

## Tests

```sh
make -C test/host      # parser tests, built with ASan/UBSan; needs ESP-IDF's cJSON at $IDF_PATH
```

## Build & flash

Requires **ESP-IDF v5.1.7**, installed at `~/esp/esp-idf`:

```sh
mkdir -p ~/esp && cd ~/esp
git clone -b v5.1.7 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
```

Build:

```sh
. ~/esp/esp-idf/export.sh
idf.py set-target esp32s3     # first time only
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

Board config lives in `sdkconfig.defaults` (`CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1=y` for the D1/D1S). If you change `sdkconfig.defaults`, delete `sdkconfig` so it gets regenerated.

### Changes from the upstream template
- Board set to D1/D1S (upstream defaults to D1L/LoRa)
- Project renamed to `trimet_station_display`
- Added the missing `#include "esp_timer.h"` in `main/model/indicator_time.c` and `indicator_btn.c`, which are required on IDF 5.1.7

## Project status

The TriMet client is running on the device and logs live arrivals. The screen still shows the template's demo UI. See [TODO.md](TODO.md).

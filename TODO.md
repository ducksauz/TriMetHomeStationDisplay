# TODO

## 0. Decisions / info needed
- [x] Get the real URL for the Indicator ESP-IDF template repo
- [x] Register at developer.trimet.org and get an `appID`
- [x] Pick stops and look up their stop IDs: 10772, 10754 (streetcar); 7601, 7763 (MAX)
- [x] Decide whether the MAX disruption shuttles (291/293) should be shown: hidden (MAX page shows light rail only)
- [x] Decide on the screen layout: Streetcar page + MAX page, alternating every 15 s; tap center to switch
- [x] Decide on the ESP-IDF and LVGL versions: ESP-IDF v5.1.7, LVGL 8.3.1 (bundled with template)

## 1. Project setup
- [x] `git init`, add `.gitignore` (build/, sdkconfig, secrets)
- [x] Import the template and confirm it builds
- [x] Flash to the D1S and confirm it boots (GX screen setting is correct)
- [x] Confirm the display works with the template's LVGL demo
- [x] Confirm touch works
- [x] Add a Kconfig menu for `appID`, stop IDs and poll interval (Wi-Fi uses the template's touchscreen setup screen)
- [x] Set your `appID` in `idf.py menuconfig`

## 2. Connectivity
- [ ] Wi-Fi station connect, with retry/backoff and a status indicator on screen
- [ ] SNTP time sync with the `America/Los_Angeles` timezone (`PST8PDT,M3.2.0,M11.1.0`)
- [x] HTTPS client using `esp_http_client` and `esp_crt_bundle`

## 3. TriMet API client
- [x] Build the Arrivals V2 request (`locIDs`, `appID`, `minutes=60`)
- [x] Parse the JSON with cJSON, reading the response into a growable PSRAM buffer (64 KB cap)
- [x] Map the response into a compact struct: stop, route, sign, `estimated`/`scheduled`, status, color
- [x] Filter which arrivals to show: by mode per page, within 90 min, 3 per stop
- [x] Confirm the `locid` field on arrivals against a live response
- [x] Handle errors (HTTP 403, malformed JSON, `resultSet.error`) and keep the last good data; back off 5 s doubling up to the poll interval
- [x] Polling task, decoupled from the UI (`trimet_client_get()` returns a snapshot)
- [x] Host unit tests for the parser (`make -C test/host`)

## 4. UI (LVGL, 480×480)
- [x] Layout: header (page title, clock), per-stop sections, arrival rows (line badge, destination, ETA), footer (status, page dots)
- [x] Line-colored badges (MAX colors come from the sign's line name, streetcar colors from `routeColor`)
- [x] Show status visually: estimated vs. scheduled-only, delayed, canceled
- [x] Recompute "minutes away" every second between polls
- [x] "Updated Ns ago" footer with stale/error states
- [ ] Verify layout on device; tune fonts/spacing
- [ ] Wire a border touch zone to the Wi-Fi setup screen (currently unreachable from the arrivals screen)
- [ ] Gate the first fetch on getting an IP address (it currently fails once with a DNS error, then retries after 5 s)
- [x] Make LVGL updates thread-safe: the UI pulls snapshots from an LVGL timer, so the data task never touches LVGL

## 5. Polish
- [ ] Dim or blank the backlight overnight, or when no service is running
- [ ] Touch to switch stops or directions (optional)
- [ ] Wi-Fi provisioning without reflashing (SoftAP or BLE) (optional)
- [ ] Watchdog and recovery; OTA updates (optional)
- [ ] Optional: show service alerts/detours via the TriMet Alerts API

## 6. Docs
- [ ] Complete the README with real build/flash steps and screenshots

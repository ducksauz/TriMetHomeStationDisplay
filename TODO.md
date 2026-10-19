# TODO

## 0. Decisions / info needed
- [x] Get the real URL for the Indicator ESP-IDF template repo
- [x] Register at developer.trimet.org and get an `appID`
- [x] Pick stops and look up their stop IDs: 10772, 10754 (streetcar); 7601, 7763 (MAX)
- [ ] Decide whether the MAX disruption shuttles (291/293) should be shown or filtered out
- [ ] Decide on the screen layout: number of stops and rows, and what to show for each arrival
- [ ] Decide on the ESP-IDF and LVGL versions (follow the template unless there's a reason not to)

## 1. Project setup
- [x] `git init`, add `.gitignore` (build/, sdkconfig, secrets)
- [ ] Import the template and confirm it builds and flashes to the D1S unchanged
- [ ] Confirm the display and touch work with the template's LVGL demo
- [ ] Add a Kconfig menu (or `secrets.h`) for Wi-Fi SSID/password, `appID` and stop IDs

## 2. Connectivity
- [ ] Wi-Fi station connect, with retry/backoff and a status indicator on screen
- [ ] SNTP time sync with the `America/Los_Angeles` timezone (`PST8PDT,M3.2.0,M11.1.0`)
- [ ] HTTPS client using `esp_http_client` and `esp_crt_bundle`

## 3. TriMet API client
- [ ] Build the Arrivals V2 request (`locIDs`, `appID`, `minutes`, `arrivals`)
- [ ] Parse the JSON with cJSON. Handle large responses: stream into PSRAM, never into a fixed small buffer.
- [ ] Map the response into a compact struct: stop, route, sign, `estimated`/`scheduled`, status, color
- [ ] Filter to streetcar and MAX routes and the chosen stops/directions
- [ ] Confirm the `locid` field on arrivals against a live response
- [ ] Handle errors (HTTP errors, malformed JSON, the `resultSet.error` field) and keep the last good data
- [ ] Polling task, every 30–60 s, decoupled from the UI

## 4. UI (LVGL, 480×480)
- [ ] Mock up the layout: header (stop name and clock), arrival rows (route badge, destination, "X min")
- [ ] Use `routeColor` for the route badges
- [ ] Show status visually: estimated vs. scheduled-only, delayed, canceled
- [ ] Recompute "minutes away" every few seconds between polls
- [ ] Add "Last updated" and stale-data warnings, plus offline/no-Wi-Fi screens
- [ ] Make LVGL updates thread-safe: lock the LVGL mutex from the data task, or post updates to the UI task

## 5. Polish
- [ ] Dim or blank the backlight overnight, or when no service is running
- [ ] Touch to switch stops or directions (optional)
- [ ] Wi-Fi provisioning without reflashing (SoftAP or BLE) (optional)
- [ ] Watchdog and recovery; OTA updates (optional)
- [ ] Optional: show service alerts/detours via the TriMet Alerts API

## 6. Docs
- [ ] Complete the README with real build/flash steps and screenshots

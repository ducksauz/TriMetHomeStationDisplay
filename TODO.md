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
- [x] Wi-Fi station connect with retry (template)
- [ ] Optional: Wi-Fi status indicator on the arrivals screen
- [x] SNTP time sync (template) with a fixed `PST8PDT,M3.2.0,M11.1.0` time zone (`CONFIG_TRIMET_TZ`)
- [x] HTTPS client using `esp_http_client` and `esp_crt_bundle`

## 3. TriMet API client
- [x] Build the Arrivals V2 request (`locIDs`, `appID`, `minutes=60`)
- [x] Parse the JSON with cJSON, reading the response into a growable PSRAM buffer (64 KB cap)
- [x] Map the response into a compact struct: stop, route, sign, `estimated`/`scheduled`, status, color
- [x] Filter which arrivals to show: by mode per page, within 60 min, 3 per stop (2 in compact layout)
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
- [x] Verify layout on device (normal layout)
- [x] Add Old Town/Chinatown (Red, Blue) as a second MAX page (stop lists split 2 per page)
- [x] Verify settings screen and Wi-Fi round trip on device
- [x] Verify the second MAX page on device
- [x] Settings cog (bottom-right) with a Wi-Fi entry
- [x] Gate fetches on having an IP address (`IP_EVENT_STA_GOT_IP`), so the first fetch no longer fails with a DNS error
- [x] Make LVGL updates thread-safe: the UI pulls snapshots from an LVGL timer, so the data task never touches LVGL

## 5. Polish
- [x] Night dimming (default 10 PM - 6 AM, 10%), configurable under Settings → Night dimming
- [x] Verify Night Dimming settings screen on device
- [x] Adjustable night brightness (1-50%, previews on the backlight while dragging)
- [x] Settings > TriMet: appID entry (on-screen keyboard), poll interval (30-120 s), page display time (5-30 s); saved in NVS, Kconfig values are defaults
- [ ] Optional: tap to temporarily brighten while dimmed
- [x] Touch to switch pages (center tap)
- [x] Wi-Fi setup without reflashing (template touchscreen screen, via Settings)
- [x] Watchdog and recovery: reboot on crash (was halt), task watchdog panics and watches the LVGL task, core dump to flash, restart after 30 min without arrivals
- [x] Restart diagnostics on the Settings screen (uptime, unexpected restart count, last reason)
- [x] Verify watchdog build on device (15 min serial capture, no warnings)
- [x] Fix the overnight reboot/blank screen: hardware SHA/AES DMA and the LCD driver flushed the PSRAM cache from both cores at once and deadlocked (interrupt watchdog); TLS now uses software SHA/AES
- [ ] Confirm no unexpected restarts over a day or two (Settings shows the count; it was 3 from testing)
- [ ] Optional: OTA updates
- [ ] Optional: show service alerts/detours via the TriMet Alerts API

## 6. Docs
- [x] Complete the README with real build/flash steps
- [ ] Add photos/screenshots to the README

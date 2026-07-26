# Skywatch — Build 28 (History + Polish)

Portable aircraft radar for the **ESP32 Cheap Yellow Display (CYD)** / ESP32-2432S028R style boards.

Skywatch shows nearby aircraft on a 320×240 landscape TFT, with touch navigation, low-latency local ADS-B, automatic OpenSky network fallback, demo traffic, a captive WiFi setup portal, a simple web dashboard, SD-card event logging, and persistent aircraft “memory” profiles.

> **Current development build:** Build 28 — History + Polish  
> Board: ESP32-2432S028R (CYD) · Display: 320×240 via TFT_eSPI

---

## Features

- **Radar UI** with sweep, heading triangles, motion trails, and smart labels
- **Manual RFRSH** touch button (status column) to force an immediate traffic pull
- **Data sources:**
  - Local ADS-B (readsb / tar1090 / dump1090) — primary, approximately 2-second updates
  - OpenSky Network — automatic fallback, HTTPS and rate-limit aware
  - Demo traffic fallback when no live source is available
- **Persistent location:** dashboard coordinates are saved in ESP32 NVS; IP geolocation is fallback only
- **Observer alerts:** NEW_TARGET / APPROACHING / THREAT_UP popup overlays (stable underlay while shown)
- **On-device event history:** LIST page tabs switch between live aircraft and recent SD events
- **SD card:** boot self-test, folder layout, splash logo, rotating event log, aircraft profiles, daily stats
- **NTP** wall-clock timestamps for SD logs (configurable timezone)
- **WiFiManager** captive portal (`Skywatch` AP)
- **Web dashboard:** `/`, `/planes`, `/coords`, `/coords/reset`
- **SPI bus ownership** (`CydSpi.h`) so SD + touch share VSPI safely
- **Heap-safe networking:** filtered streaming for local readsb JSON; radar sprite paused during OpenSky TLS and IP geo HTTP
- **Runtime receiver configuration:** local ADS-B URL can be changed on the SD card without recompiling

---

## Hardware

| Item | Notes |
|------|--------|
| Board | ESP32-2432S028R / common 2.8" Cheap Yellow Display |
| Display | 320×240 landscape, **TFT_eSPI** (match your CYD pinout) |
| Touch | XPT2046 (enable/calibrate in `config.h`) |
| Storage | microSD — logo, events, `/planes` profiles, `/stats` |
| Network | WiFi for OpenSky / web dashboard; optional LAN ADS-B |

---

## Flash (Arduino IDE)

1. Libraries: **TFT_eSPI**, **WiFiManager**, **XPT2046_Touchscreen**, **ArduinoJson**
2. Open `Skywatch_CYD_build28_history_polish.ino`
3. Board: your ESP32 CYD profile · **Partition Scheme: Huge App**
4. Copy `sd_card_files/` to the microSD and set `LocalADSBURL` in `/config/settings.txt`
5. Upload

---

## Configuration (`config.h`)

| Setting | Purpose |
|---------|---------|
| `BUILD_LABEL` | Shown on system / boot screens |
| `SKYWATCH_DATA_SOURCE` | `AUTO` / `LOCAL` / `OPENSKY` / `DEMO` |
| `ENABLE_LOCAL_ADSB` / `LOCAL_ADSB_URL_DEFAULT` | Compiled local receiver fallback |
| `OPENSKY_CALL_MODE` | Manual / 60s / 360s |
| `TIMEZONE` | POSIX TZ for NTP (e.g. `UTC0`, `PST8PDT,M3.2.0,M11.1.0`) |
| `AIRCRAFT_MEMORY_WRITE_INTERVAL_MS` | How often profiles flush to SD |
| `SPLASH_BMP_SWAP_BYTES` | Splash logo RGB565 byte order (usually `1` on CYD) |
| `SPLASH_BMP_INVERT` / `SPLASH_BMP_SWAP_RB` | Optional logo color tweaks |
| `REFRESH_BTN_*` | Manual refresh hit-box (status column) |

### Local ADS-B receiver

Build 27 defaults to `AUTO`: it checks the local receiver first, keeps a healthy
local feed authoritative, and uses OpenSky when the receiver cannot be reached.

Edit the SD card file `/config/settings.txt`:

```text
LocalADSBURL=http://192.168.1.64:8080/tar1090/data/aircraft.json
```

Replace the address with the LAN IP and port of your readsb/tar1090 host. The URL
must be reachable from another device on the same network. Local positions older
than 120 seconds are ignored. A healthy feed is polled every 2 seconds; failed
connections back off for 30 seconds so the display remains responsive.

---

## microSD layout

Copy `sd_card_files/` to the **root** of a FAT microSD:

```
/config/settings.txt         ← local ADS-B URL override
/logos/skywatch_logo.bmp    ← 24-bit uncompressed BMP, 320×240 recommended
/events/
/tracks/
/planes/
/stats/
```

If the logo is missing or invalid, a vector splash is used instead.

---

## Project layout

```
.
├── Skywatch_CYD_build28_history_polish.ino
├── config.h
├── CydSpi.h                 # SD / touch VSPI ownership
├── SkywatchData.*           # traffic sources, alerts, OpenSky, IP geo
├── DisplayCYD.*             # TFT UI, radar sprite, splash BMP
├── InputCYD.*               # touch + RFRSH hit testing
├── WebDashboard.*
├── SkywatchStorage.*        # SD init, events, aircraft memory
├── skywatch_logo.bmp
└── sd_card_files/
```

---

## Build 28 notes

- Manual latitude/longitude entered on the web dashboard are saved in ESP32 NVS
  and restored before IP geolocation runs
- `Use IP location` clears the saved override explicitly; ISP geolocation is never
  allowed to overwrite a saved manual position at boot
- The LIST page now contains `LIVE AIRCRAFT` and `HISTORY` touch tabs
- New event records include the NTP wall-clock time when available
- History reads only the final 3072 bytes of the log and retains 20 fixed-size
  parsed entries; it never loads the complete event file into heap memory
- Existing Build 26/27 event lines remain readable
- The active event log rotates at 256 KB to bound SD scan and storage growth
- Build 27 local ADS-B, OpenSky fallback, aircraft awareness, SD memory, splash,
  touch navigation, and heap protections remain intact

### Location behavior

The boot priority is:

1. A manual location previously saved through the web dashboard
2. IP geolocation when no saved location exists

The SYSTEM page and web dashboard show the active location source. Normal firmware
uploads preserve the saved NVS coordinates; erasing the full ESP32 flash clears them.

### Event history

Tap `LIST`, then choose `HISTORY`. Skywatch displays the newest stored events first
with time, event type, callsign, distance, CPA, and threat level. The page refreshes
from the SD tail every five seconds while visible. Event types include
`NEW_TARGET`, `APPROACHING`, and `THREAT_UP`.

## License / ownership

Private project — all rights reserved by the repository owner unless otherwise noted.

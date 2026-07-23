# Skywatch — Build 27 (Local ADS-B)

Portable aircraft radar for the **ESP32 Cheap Yellow Display (CYD)** / ESP32-2432S028R style boards.

Skywatch shows nearby aircraft on a 320×240 landscape TFT, with touch navigation, low-latency local ADS-B, automatic OpenSky network fallback, demo traffic, a captive WiFi setup portal, a simple web dashboard, SD-card event logging, and persistent aircraft “memory” profiles.

> **Current release:** Build 27 — Local ADS-B  
> Board: ESP32-2432S028R (CYD) · Display: 320×240 via TFT_eSPI

---

## Features

- **Radar UI** with sweep, heading triangles, motion trails, and smart labels
- **Manual RFRSH** touch button (status column) to force an immediate traffic pull
- **Data sources:**
  - Local ADS-B (readsb / tar1090 / dump1090) — primary, approximately 2-second updates
  - OpenSky Network — automatic fallback, HTTPS and rate-limit aware
  - Demo traffic fallback when no live source is available
- **IP geolocation** for home lat/lon after WiFi (with retry + fallback provider); manual override via web `/coords`
- **Observer alerts:** NEW_TARGET / APPROACHING / THREAT_UP popup overlays (stable underlay while shown)
- **SD card:** boot self-test, folder layout, splash logo, event log, aircraft profiles, daily stats
- **NTP** wall-clock timestamps for SD logs (configurable timezone)
- **WiFiManager** captive portal (`Skywatch` AP)
- **Web dashboard:** `/`, `/planes`, `/coords`
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
2. Open `Skywatch_CYD_build27_local_adsb.ino`
3. Board: your ESP32 CYD profile · **Partition Scheme: Huge App**
4. Copy `sd_card_files/` to the microSD and set `LocalADSBURL` in `/config/settings.txt              ← local ADS-B URL override`
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
LocalADSBURL=http://192.168.1.220:8080/tar1090/data/aircraft.json
```

Replace the address with the LAN IP and port of your readsb/tar1090 host. The URL
must be reachable from another device on the same network. Local positions older
than 30 seconds are ignored. A healthy feed is polled every 2 seconds; failed
connections back off for 30 seconds so the display remains responsive.

---

## microSD layout

Copy `sd_card_files/` to the **root** of a FAT microSD:

```
/config/settings.txt
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
├── Skywatch_CYD_build27_local_adsb.ino
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

## Build 27 notes

- Local readsb/tar1090 is enabled by default in `AUTO` mode
- The tested workshop endpoint is supplied through SD `settings.txt`
- Local JSON is filtered and parsed directly from the HTTP stream; the complete
  response is never duplicated into a large `String`
- `seen_pos` rejects stale coordinates before they reach the radar
- Local success uses a 2-second cadence; failure uses a 30-second backoff
- OpenSky is allowed immediately at boot or after local failure, then follows its
  configured rate-safe interval
- Build 26 aircraft memory, SD logging, splash, touch, and heap-safe TLS behavior
  remain intact

## License / ownership

Private project — all rights reserved by the repository owner unless otherwise noted.

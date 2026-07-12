# Skywatch — Build 26 (Memory)

Portable aircraft radar for the **ESP32 Cheap Yellow Display (CYD)** / ESP32-2432S028R style boards.

Skywatch shows nearby aircraft on a 320×240 landscape TFT, with touch navigation, optional local ADS-B, OpenSky network fallback, demo traffic, a captive WiFi setup portal, a simple web dashboard, SD-card event logging, and persistent aircraft “memory” profiles.

> **Current release:** Build 26 — Memory  
> Board: ESP32-2432S028R (CYD) · Display: 320×240 via TFT_eSPI

---

## Features

- **Radar UI** with sweep, heading triangles, motion trails, and smart labels
- **Manual RFRSH** touch button (status column) to force an immediate traffic pull
- **Data sources:**
  - Local ADS-B (tar1090 / dump1090) — optional
  - OpenSky Network (HTTPS, rate-limit aware)
  - Demo traffic fallback
- **Observer alerts:** NEW_TARGET / APPROACHING / THREAT_UP popup overlays (stable underlay while shown)
- **SD card:** boot self-test, folder layout, splash logo, event log, aircraft profiles, daily stats
- **NTP** wall-clock timestamps for SD logs (configurable timezone)
- **WiFiManager** captive portal (`Skywatch` AP)
- **Web dashboard:** `/`, `/planes`, `/coords`
- **SPI bus ownership** (`CydSpi.h`) so SD + touch share VSPI safely
- **Heap-safe OpenSky:** radar sprite paused during TLS so HTTPS works on CYD RAM limits

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
2. Open `Skywatch_CYD_build26_memory.ino`
3. Board: your ESP32 CYD profile · **Partition Scheme: Huge App**
4. Configure `config.h` (source mode, OpenSky interval, timezone, touch, SD)
5. Upload

---

## Configuration (`config.h`)

| Setting | Purpose |
|---------|---------|
| `BUILD_LABEL` | Shown on system / boot screens |
| `SKYWATCH_DATA_SOURCE` | `AUTO` / `LOCAL` / `OPENSKY` / `DEMO` |
| `ENABLE_LOCAL_ADSB` / `LOCAL_ADSB_URL` | Local receiver |
| `OPENSKY_CALL_MODE` | Manual / 60s / 360s |
| `TIMEZONE` | POSIX TZ for NTP (e.g. `UTC0`, `PST8PDT,M3.2.0,M11.1.0`) |
| `AIRCRAFT_MEMORY_WRITE_INTERVAL_MS` | How often profiles flush to SD |
| `SPLASH_BMP_SWAP_BYTES` | Splash logo RGB565 byte order (usually `1` on CYD) |
| `SPLASH_BMP_INVERT` / `SPLASH_BMP_SWAP_RB` | Optional logo color tweaks |
| `REFRESH_BTN_*` | Manual refresh hit-box (status column) |

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
├── Skywatch_CYD_build26_memory.ino
├── config.h
├── CydSpi.h                 # SD / touch VSPI ownership
├── SkywatchData.*           # traffic sources, alerts, OpenSky
├── DisplayCYD.*             # TFT UI, radar sprite, splash BMP
├── InputCYD.*               # touch + RFRSH hit testing
├── WebDashboard.*
├── SkywatchStorage.*        # SD init, events, aircraft memory
├── skywatch_logo.bmp
└── sd_card_files/
```

---

## Build 26 notes

- Aircraft profiles: `/planes/<ID>.txt` (sighting count uses a gap threshold, not every flush)
- Daily snapshot: `/stats/today.txt`
- OpenSky uses **HTTPS** + temporary radar-sprite release for TLS heap
- Rate-limit **429** is not double-retried
- Alert popups freeze the underlay (no FPS flash-through)
- Radar chrome (status / RFRSH / menu) updates ~1 Hz; scope still animates

---

## License / ownership

Private project — all rights reserved by the repository owner unless otherwise noted.

# Skywatch — Build 25 (Observer)

Portable aircraft radar for the **ESP32 Cheap Yellow Display (CYD)** / ESP32-2432S028R style boards.

Skywatch shows nearby aircraft on a 320×240 landscape TFT, with touch navigation, optional local ADS-B, OpenSky network fallback, demo traffic, a captive WiFi setup portal, a simple web dashboard, and SD-card event logging.

> **Current release:** Build 25 — Observer  
> Board: ESP32-2432S028R (CYD) · Display: 320×240 via TFT_eSPI

---

## Features

- **Radar UI** with sweep, heading triangles, motion trails, and smart labels
- **Data sources (AUTO mode):**
  1. Local ADS-B (tar1090 / dump1090) — disabled by default until your receiver is ready
  2. OpenSky Network (rate-limited safe mode)
  3. Demo traffic so the UI always has something to show
- **Observer alerts:** NEW_TARGET / APPROACHING / THREAT_UP popup overlays
- **SD card support:** boot self-test, folder layout, event log, optional splash logo
- **WiFiManager** captive portal (`Skywatch` AP) for network setup
- **Web dashboard** endpoints: `/`, `/planes`, `/coords`
- **Battery** voltage readout on supported hardware
- **Touch navigation** (XPT2046) for bottom-bar buttons

---

## Hardware

| Item | Notes |
|------|--------|
| Board | ESP32-2432S028R / common 2.8" Cheap Yellow Display |
| Display | 320×240 landscape, driven by **TFT_eSPI** (must match your CYD pinout) |
| Touch | XPT2046 (enable/calibrate in `config.h`) |
| Storage | microSD (optional) — logo + event log |
| Network | WiFi for OpenSky / web dashboard; optional LAN ADS-B URL |

---

## Flash (Arduino IDE)

1. Install board support for ESP32 and libraries:
   - **TFT_eSPI** (configure `User_Setup` for your CYD)
   - **WiFiManager**
   - **XPT2046_Touchscreen** (if touch is enabled)
   - Arduino JSON (if required by your library set)
2. Open `Skywatch_CYD_build25_observer.ino`
3. Board: your ESP32 CYD profile  
4. **Partition Scheme:** Huge App  
5. Set options in `config.h` (data source, local ADS-B URL, OpenSky mode, touch, SD)
6. Upload

---

## Configuration (`config.h`)

Key knobs:

| Setting | Purpose |
|---------|---------|
| `SKYWATCH_DATA_SOURCE` | `AUTO` / `LOCAL` / `OPENSKY` / `DEMO` |
| `ENABLE_LOCAL_ADSB` | Turn on when your RTL-SDR / tar1090 endpoint is live |
| `LOCAL_ADSB_URL` | e.g. `http://192.168.x.x/tar1090/data/aircraft.json` |
| `OPENSKY_CALL_MODE` | Manual / 60s / 360s safe interval |
| `ENABLE_TOUCH_NAV` | Bottom-bar touch buttons |
| `ENABLE_SD_STORAGE` | SD logo + event logging |
| `ENABLE_OBSERVER_ALERTS` | Alert popup overlays |
| `WIFI_PORTAL_SSID` | Captive portal AP name (default `Skywatch`) |

Local ADS-B stays **off** by default so a dead LAN endpoint cannot stall the UI.

---

## microSD layout

Copy everything under `sd_card_files/` to the **root** of a FAT-formatted microSD card:

```
/config/settings.txt
/logos/skywatch_logo.bmp
/events/          (created/used for event log)
/tracks/
/planes/
/stats/
```

Most important file: `/logos/skywatch_logo.bmp`  
If the logo is missing, Skywatch falls back to a built-in vector splash.

On boot, Build 25:

- Self-tests the SD card
- Ensures the folder structure exists
- Logs observer events to `/events/events.txt` when SD is ready

If SD fails, the device still runs — it reports **SD NOT FOUND** and skips logo/file logging.

---

## Project layout

```
.
├── Skywatch_CYD_build25_observer.ino   # main sketch
├── config.h                            # board / feature flags
├── SkywatchData.*                      # aircraft, location, sources, events
├── DisplayCYD.*                        # TFT pages, radar, alerts, system check
├── InputCYD.*                          # touch input
├── WebDashboard.*                      # HTTP UI
├── SkywatchStorage.*                   # SD init + event log
├── skywatch_logo.bmp                   # logo (also under sd_card_files)
└── sd_card_files/                      # copy to microSD root
```

---

## Runtime behavior (Build 25)

1. Splash (+ optional SD logo)
2. System check screen (WiFi / SD / source status)
3. Radar + list / traffic / system pages via touch bar
4. Plane updates throttled per source; radar redraw ~8 FPS
5. Observer alerts as popup overlays; events queued in RAM and flushed to SD

---

## Web dashboard

After WiFi connects, open the device IP in a browser:

| Path | Content |
|------|---------|
| `/` | Status overview |
| `/planes` | Aircraft list |
| `/coords` | Location / coordinates |

---

## Notes

- Configure **TFT_eSPI** for your specific CYD pinout before first flash.
- OpenSky is kept in a conservative interval mode to avoid rate limits.
- Demo fallback keeps the UI alive when no live traffic is available.
- This repo currently tracks **Build 25 (Observer)** only.

---

## License / ownership

Private project — all rights reserved by the repository owner unless otherwise noted.

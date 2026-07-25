SKYWATCH CYD - Build 28 History + Polish

Flash notes:
- Board: ESP32 CYD / ESP32-2432S028R style board
- Partition Scheme: Huge App
- TFT_eSPI must be configured for your CYD display.
- Open Skywatch_CYD_build28_history_polish.ino.

Build 28:
- Coordinates saved through the web dashboard persist across reboots.
- Saved manual coordinates take priority over IP geolocation.
- The dashboard can intentionally clear the saved location and return to IP lookup.
- LIST contains LIVE AIRCRAFT and HISTORY touch tabs.
- HISTORY reads a fixed SD tail buffer and never loads the full event log.
- Event logs include wall-clock timestamps after NTP synchronization.
- The event log rotates at 256 KB.

Build 27 foundation preserved:
- AUTO mode uses local readsb/tar1090 first and OpenSky as fallback.
- Healthy local feeds update every 2 seconds.
- Failed local connections back off for 30 seconds.
- OpenSky is allowed immediately when the local receiver is unavailable.
- Local JSON is filtered and parsed directly from the HTTP stream.
- Positions older than 120 seconds are discarded using seen_pos.
- The local receiver URL can be changed on the SD card without recompiling.
- Build 26 aircraft profiles, daily stats, SD logging, NTP, touch, splash,
  observer alerts, and heap-safe OpenSky TLS behavior are preserved.

Local ADS-B setup:
1) Copy sd_card_files to the root of a FAT microSD card.
2) Edit /config/settings.txt.
3) Set:
   LocalADSBURL=http://YOUR_RECEIVER_IP:PORT/tar1090/data/aircraft.json
4) Confirm that URL opens from another device on the same network.
5) Insert the SD card and boot Skywatch.

Tested workshop endpoint:
  http://192.168.1.64:8080/tar1090/data/aircraft.json

Data source order:
  LOCAL ADS-B -> OpenSky -> Demo

SD layout:
  /config/settings.txt
  /logos/skywatch_logo.bmp
  /events
  /tracks
  /planes
  /stats

Timezone:
Edit TIMEZONE in config.h (POSIX string). Examples:
  UTC0
  PST8PDT,M3.2.0,M11.1.0
  EST5EDT,M3.2.0,M11.1.0

If the SD card is missing:
Skywatch uses LOCAL_ADSB_URL_DEFAULT from config.h and continues running.
Aircraft memory, event logging, and the SD splash are disabled.

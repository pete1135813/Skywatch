SKYWATCH CYD - Build 26 Memory (fixed)

Flash notes:
- Board: ESP32 CYD / ESP32-2432S028R style board
- Partition Scheme: Huge App
- TFT_eSPI must already be configured for your CYD display.

New in Build 25:
- SD card self-test during boot
- Automatic SD folder structure:
  /events /tracks /planes /stats /config /logos
- Optional SD splash bitmap at /logos/skywatch_logo.bmp
- Startup system check screen
- RAM event queue
- SD event logging to /events/events.txt
- Alert popup overlay for NEW_TARGET / APPROACHING / THREAT_UP
- SYSTEM page reports SD status and event count

Build 26 additions:
- SD aircraft profile memory in /planes/<CALLSIGN>.txt
- SD daily snapshot in /stats/today.txt
- Aircraft memory writes on a 2-minute interval (configurable)
- NTP wall-clock timestamps for SD logs (set TIMEZONE in config.h)
- SPI bus ownership helper (CydSpi.h) so SD + touch share VSPI safely
- OpenSky uses https:// with WiFiClientSecure (required)
- Manual RFRSH touch button on the radar screen (lower-left) to force
  an immediate traffic pull without waiting for the OpenSky/local timer

Build 26 fixes (vs first memory draft):
1) Touch no longer permanently steals VSPI from the microSD slot.
   Every SD access claims the SD pin map; every touch read claims touch pins.
2) Aircraft profile load is one open/scan per plane (not 5).
3) "Seen" increments only after a 5-minute gap (AIRCRAFT_SEEN_GAP_SEC),
   not on every flush.
4) OpenSky URL restored to https:// (http:// + WiFiClientSecure was broken).
5) NTP sync after WiFi connect; boot-relative timestamps until sync.

SD setup:
Copy the contents of the included sd_card_files folder to the root of your microSD card.
The most important file is:
  /logos/skywatch_logo.bmp
If the logo file is missing, Skywatch will use the built-in vector fallback splash.

Current source behavior:
SKYWATCH_DATA_SOURCE defaults to OPENSKY for development testing.
OpenSky is set to 60-second development mode. Change OPENSKY_CALL_MODE in
config.h back to OPENSKY_MODE_360_SEC for long unattended observer runs.
Local ADS-B remains available via AUTO / LOCAL when ENABLE_LOCAL_ADSB is on.

Timezone:
Edit TIMEZONE in config.h (POSIX string). Examples:
  UTC0
  PST8PDT,M3.2.0,M11.1.0
  EST5EDT,M3.2.0,M11.1.0

If SD fails:
Skywatch still runs normally. It simply shows SD NOT FOUND and skips file logging/logo loading.

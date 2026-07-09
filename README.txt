SKYWATCH CYD - Build 25 Observer

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

SD setup:
Copy the contents of the included sd_card_files folder to the root of your microSD card.
The most important file is:
  /logos/skywatch_logo.bmp
If the logo file is missing, Skywatch will use the built-in vector fallback splash.

Current source behavior:
AUTO = LOCAL ADS-B > OpenSky safe mode > DEMO fallback.
Local ADS-B is still disabled by default in config.h until your RTL-SDR endpoint is ready.
OpenSky remains in 360-second safe mode to avoid rate limits.

Local ADS-B endpoint placeholder in config.h:
  http://192.168.1.220/tar1090/data/aircraft.json

Event log:
When alerts are generated, entries append to:
  /events/events.txt

If SD fails:
Skywatch still runs normally. It simply shows SD NOT FOUND and skips file logging/logo loading.

#pragma once

// ===================== SKYWATCH CYD CONFIG =====================
// Target board: ESP32-2432S028R / common 2.8" Cheap Yellow Display
// Display: 320x240 landscape through TFT_eSPI
// NOTE: TFT_eSPI User_Setup must match your CYD pinout.

#include <Arduino.h>

static const char BUILD_LABEL[] = "Build 26 Memory";

// ---------------------- DISPLAY ----------------------
static const int SCREEN_WIDTH  = 320;
static const int SCREEN_HEIGHT = 240;

// Landscape layout zones
static const int TOP_BAR_H      = 22;
static const int TOUCH_BAR_Y    = 206;
static const int TOUCH_BAR_H    = 34;
static const int RADAR_CENTER_X = 114;
static const int RADAR_CENTER_Y = 114;
static const int RADAR_RADIUS   = 76;
static const int STATUS_PANEL_X = 224;

// Backward-compatible aliases used by splash/status screens
static const int CENTER_X = SCREEN_WIDTH / 2;
static const int CENTER_Y = SCREEN_HEIGHT / 2;

// ---------------------- TOUCH NAV ----------------------
// Leave this false until the XPT2046_Touchscreen library is installed and calibrated.
// When true, bottom buttons become touchable.
#define ENABLE_TOUCH_NAV 1

// Common ESP32-2432S028R CYD touch pins. If your CYD variant differs, change these.
static const int TOUCH_CS   = 33;
static const int TOUCH_IRQ  = 36;
static const int TOUCH_MOSI = 32;
static const int TOUCH_MISO = 39;
static const int TOUCH_CLK  = 25;

// Raw calibration values. These are decent starting values, but every panel is a little different.
static const int TOUCH_MIN_X = 250;
static const int TOUCH_MAX_X = 3800;
static const int TOUCH_MIN_Y = 250;
static const int TOUCH_MAX_Y = 3800;


// ---------------------- DATA SOURCES ----------------------
// AUTO tries LOCAL first, then OpenSky only at the selected safe interval.
// If both fail, demo traffic keeps the UI alive for testing.
#define DATA_SOURCE_AUTO     0
#define DATA_SOURCE_LOCAL    1
#define DATA_SOURCE_OPENSKY  2
#define DATA_SOURCE_DEMO     3

#define SKYWATCH_DATA_SOURCE DATA_SOURCE_OPENSKY

// Local ADS-B receiver endpoints usually look like one of these:
//   http://192.168.1.xxx/tar1090/data/aircraft.json
//   http://192.168.1.xxx/dump1090-fa/data/aircraft.json
//   http://192.168.1.xxx/data/aircraft.json
static const char LOCAL_ADSB_URL[] = "http://192.168.1.220/tar1090/data/aircraft.json";
// Keep this OFF until your RTL-SDR/tar1090/readsb endpoint is actually running.
// A dead local endpoint can stall the UI if we probe it too aggressively.
#define ENABLE_LOCAL_ADSB 0
static const unsigned long LOCAL_ADSB_INTERVAL_MS = 30000;
static const unsigned long LOCAL_ADSB_FAIL_BACKOFF_MS = 30000;
static const uint16_t LOCAL_ADSB_HTTP_TIMEOUT_MS = 250;

#define OPENSKY_MODE_MANUAL_ONLY 0
#define OPENSKY_MODE_60_SEC      1
#define OPENSKY_MODE_360_SEC     2

#define OPENSKY_CALL_MODE OPENSKY_MODE_60_SEC
static const unsigned long OPENSKY_60_INTERVAL_MS  = 60000;
static const unsigned long OPENSKY_360_INTERVAL_MS = 360000;
static const bool ENABLE_DEMO_FALLBACK = true;

// ---------------------- BATTERY ----------------------
static const int BATTERY_PIN = 34;
static const float BATTERY_MIN_V = 3.3f;
static const float BATTERY_MAX_V = 4.2f;
static const float BATTERY_DIVIDER_MULTIPLIER = 1.0f; // change to 2.0 if using a half-voltage divider

// ---------------------- PLANES ----------------------
static const int MAX_PLANES = 10;
static const float MAX_RANGE_NM = 20.0f;
static const int TRAIL_POINTS = 10;        // previous radar positions kept per aircraft
static const bool ENABLE_MOTION_TRAILS = true;
static const bool ENABLE_SMART_LABELS = true;
static const bool ENABLE_RADAR_SWEEP = true;
static const bool ENABLE_HEADING_TRIANGLES = true;

// ---------------------- TIMERS ----------------------
static const unsigned long PLANE_UPDATE_INTERVAL_MS   = 1000;  // quick scheduler tick; source-specific throttles prevent network spam
static const unsigned long RADAR_REDRAW_INTERVAL_MS   = 120;   // buffered radar/sweep refresh, about 8 FPS
static const unsigned long PAGE_REDRAW_INTERVAL_MS    = 1000;  // list/traffic/system refresh, keeps touch UI snappy
static const unsigned long BATTERY_UPDATE_INTERVAL_MS = 10000;

// ---------------------- UI ----------------------
static const bool AUTO_CYCLE_SCREENS = false;
static const unsigned long AUTO_CYCLE_INTERVAL_MS = 8000;

// Manual traffic refresh button — MUST sit outside the radar sprite footprint
// (sprite is x=0..STATUS_PANEL_X-1). Right-hand status column stays steady.
static const int REFRESH_BTN_X = 228;
static const int REFRESH_BTN_Y = 170;
static const int REFRESH_BTN_W = 86;
static const int REFRESH_BTN_H = 18;

// ---------------------- SD / STORAGE ----------------------
#define ENABLE_SD_STORAGE 1
// CYD microSD on VSPI (shared peripheral with touch — see CydSpi.h)
static const int SD_CS_PIN   = 5;
static const int SD_SCK_PIN  = 18;
static const int SD_MISO_PIN = 19;
static const int SD_MOSI_PIN = 23;
// Hold TFT CS high while using SD (typical CYD TFT_CS = GPIO 15).
static const int TFT_CS_PIN  = 15;
// 10 MHz is a safe default for CYD microSD after TFT_eSPI has been started.
static const uint32_t SD_SPI_HZ = 10000000;
static const char SD_LOGO_PATH[] = "/logos/skywatch_logo.bmp";
static const char SD_LOGO_PATH_ALT[] = "/skywatch_logo.bmp";
static const char SD_EVENT_LOG_PATH[] = "/events/events.txt";
// Splash BMP color fixes (24-bit BMP is BGR on disk; CYD/TFT_eSPI often needs tweaks):
//   SWAP_BYTES — required for most TFT_eSPI pushImage RGB565 buffers (try this first)
//   INVERT     — photographic negative look (black<->white); set 0 if only hues were wrong
//   SWAP_RB    — red/blue channel swap if sky is orange / skin is blue
#define SPLASH_BMP_SWAP_BYTES 1
#define SPLASH_BMP_INVERT     0
#define SPLASH_BMP_SWAP_RB    0

// How often to flush aircraft profiles / daily stats to SD (lighter writes are safer).
static const unsigned long AIRCRAFT_MEMORY_WRITE_INTERVAL_MS = 120000; // 2 minutes
// Reappearance after this gap counts as a new "Seen" sighting (not every flush).
static const unsigned long AIRCRAFT_SEEN_GAP_SEC = 300; // 5 minutes

// Observer / alert behavior
#define ENABLE_OBSERVER_ALERTS 1
static const unsigned long ALERT_POPUP_MS = 5000;
static const unsigned long OBSERVER_IDLE_MS = 300000;

// ---------------------- WIFI ----------------------
static const char WIFI_PORTAL_SSID[] = "Skywatch";

// ---------------------- NTP / TIME ----------------------
// Used for SD profile timestamps and event logs once WiFi is up.
static const char NTP_SERVER_1[] = "pool.ntp.org";
static const char NTP_SERVER_2[] = "time.google.com";
// POSIX TZ string. Examples:
//   "UTC0"
//   "PST8PDT,M3.2.0,M11.1.0"   // US Pacific
//   "EST5EDT,M3.2.0,M11.1.0"   // US Eastern
//   "CST6CDT,M3.2.0,M11.1.0"   // US Central
//   "MST7MDT,M3.2.0,M11.1.0"   // US Mountain
static const char TIMEZONE[] = "UTC0";
static const unsigned long NTP_SYNC_TIMEOUT_MS = 8000;

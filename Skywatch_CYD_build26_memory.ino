/*
   SKYWATCH v2.1 CYD Port — Build 26 Memory (fixed)

   Target:
   - ESP32 Cheap Yellow Display / ESP32-2432S028R style board
   - 320x240 TFT using TFT_eSPI

   Preserved from original sketch:
   - WiFiManager captive portal "Skywatch"
   - IP geolocation fallback
   - OpenSky nearby aircraft fetch
   - Web dashboard with /, /planes, /coords
   - Battery monitor

   Build 26:
   - SD aircraft profile memory + daily stats
   - SPI bus ownership between SD and touch (CydSpi.h)
   - NTP wall-clock timestamps for SD logs
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include "config.h"
#include "SkywatchData.h"
#include "DisplayCYD.h"
#include "WebDashboard.h"
#include "InputCYD.h"
#include "SkywatchStorage.h"

SkywatchData skywatch;
DisplayCYD display;
WebDashboard web;
InputCYD input;
SkywatchStorage storage;
WiFiManager wm;

unsigned long lastPlaneUpdate = 0;
unsigned long lastUiRedraw = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastMemoryUpdate = 0;
ScreenMode lastScreen = SCREEN_RADAR;
bool ntpSynced = false;

bool syncNtpTime() {
  Serial.println(F("[NTP] Starting sync..."));
  setenv("TZ", TIMEZONE, 1);
  tzset();
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);

  const unsigned long start = millis();
  while (millis() - start < NTP_SYNC_TIMEOUT_MS) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      struct tm tmInfo;
      localtime_r(&now, &tmInfo);
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tmInfo);
      Serial.print(F("[NTP] OK: "));
      Serial.println(buf);
      return true;
    }
    delay(200);
  }
  Serial.println(F("[NTP] Timed out — SD timestamps will use boot-relative time until sync."));
  return false;
}

void updateAircraftMemory() {
  if (!storage.isReady()) return;
  // Spread SD work: one plane at a time with a tiny yield so WiFi/web stay alive.
  for (int i = 0; i < skywatch.getPlaneCount(); i++) {
    storage.recordAircraft(skywatch.getPlane(i));
    yield();
    web.handleClient();
  }
  storage.writeDailyStats(skywatch);
}

// Called only around OpenSky HTTPS so mbedTLS has contiguous heap for TLS.
void heapPauseForNetwork() { display.pauseHeavyBuffers(); }
void heapResumeAfterNetwork() { display.resumeHeavyBuffers(); }

void logPendingEvents() {
  if (!storage.isReady()) return;
  for (int i = skywatch.getEventCount() - 1; i >= 0; i--) {
    const SkyEvent &e = skywatch.getEvent(i);
    if (!e.logged) {
      storage.logEvent(skywatch.eventLogLine(i));
      skywatch.markEventLogged(i);
    }
  }
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("[WiFiManager] Entered config mode");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("SSID: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  display.showPortalInstructions();
}

void saveConfigCallback() {
  Serial.println("[WiFiManager] Configuration saved.");
}

void setup() {
  Serial.begin(115200);
  delay(800); // give USB serial monitor time to attach so [SD]/[BMP] lines are visible
  Serial.println();
  Serial.println(F("========== SKYWATCH BOOT =========="));
  Serial.println(BUILD_LABEL);

  display.begin();
  storage.begin();
  display.setStorageStatus(storage.isReady(), storage.statusText());
  // Splash runs BEFORE radar sprite alloc so SD logo has full RAM and a quiet SPI bus.
  display.showSplash();
  delay(400);
  display.ensureRadarSprite();

  skywatch.begin();
  input.begin();
  display.showSystemCheck(skywatch);
  if (!display.splashLogoOk()) {
    Serial.println(F("[BMP] Hint: use 24-bit uncompressed BMP at /logos/skywatch_logo.bmp"));
  }
  delay(1200);

  display.showConnectingScreen();

  wm.setAPCallback(configModeCallback);
  wm.setSaveConfigCallback(saveConfigCallback);

  if (!wm.autoConnect(WIFI_PORTAL_SSID)) {
    Serial.println("Failed to connect and hit timeout. Rebooting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  ntpSynced = syncNtpTime();

  // Free radar sprite (~85KB) only while doing OpenSky TLS — fixes HTTP -1 on CYD.
  skywatch.setNetworkHeapHooks(heapPauseForNetwork, heapResumeAfterNetwork);

  if (!skywatch.coordsWereOverridden()) {
    skywatch.fetchHomeLocationFromIP();
  }

  web.begin(&skywatch);

  skywatch.updateBattery();
  skywatch.updatePlanes();
  logPendingEvents();
  updateAircraftMemory();
  display.drawScreen(input.currentScreen(), skywatch);

  lastPlaneUpdate = millis();
  lastUiRedraw = millis();
  lastBatteryUpdate = millis();
  lastMemoryUpdate = millis();
}

void loop() {
  web.handleClient();
  input.update();

  unsigned long now = millis();

  // Late NTP retry if first attempt failed (e.g. DNS slow at boot).
  if (!ntpSynced && WiFi.status() == WL_CONNECTED) {
    static unsigned long lastNtpTry = 0;
    static uint8_t ntpTries = 0;
    if (ntpTries < 5 && now > 30000UL && now - lastNtpTry > 60000UL) {
      lastNtpTry = now;
      ntpTries++;
      ntpSynced = syncNtpTime();
    }
  }

  // Radar RFRSH button: force an immediate traffic pull (bypasses OpenSky/local timers).
  if (input.consumeRefreshRequest()) {
    skywatch.forceTrafficRefresh();
    skywatch.updatePlanes();
    logPendingEvents();
    lastPlaneUpdate = millis();
    // Force a UI redraw so the button shows the pending state / new age.
    lastUiRedraw = 0;
  }

  if (now - lastPlaneUpdate >= PLANE_UPDATE_INTERVAL_MS) {
    lastPlaneUpdate = now;
    skywatch.updatePlanes();
    logPendingEvents();
  }

  if (now - lastMemoryUpdate >= AIRCRAFT_MEMORY_WRITE_INTERVAL_MS) {
    lastMemoryUpdate = now;
    updateAircraftMemory();
  }

  if (now - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL_MS) {
    lastBatteryUpdate = now;
    skywatch.updateBattery();
  }

  ScreenMode screen = input.currentScreen();
  bool screenChanged = (screen != lastScreen);
  unsigned long redrawInterval = (screen == SCREEN_RADAR) ? RADAR_REDRAW_INTERVAL_MS : PAGE_REDRAW_INTERVAL_MS;

  if (screenChanged || now - lastUiRedraw >= redrawInterval) {
    lastUiRedraw = now;
    lastScreen = screen;
    display.drawScreen(screen, skywatch);
  }
}

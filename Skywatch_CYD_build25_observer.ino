/*
   SKYWATCH v2.1 CYD Port

   Target:
   - ESP32 Cheap Yellow Display / ESP32-2432S028R style board
   - 320x240 TFT using TFT_eSPI

   Preserved from original sketch:
   - WiFiManager captive portal "Skywatch"
   - IP geolocation fallback
   - OpenSky nearby aircraft fetch
   - Web dashboard with /, /planes, /coords
   - Battery monitor

   New structure:
   - SkywatchData: aircraft, location, battery, OpenSky fetch
   - DisplayCYD: CYD display pages
   - WebDashboard: web interface
   - InputCYD: placeholder for future touchscreen navigation
*/

#include <WiFi.h>
#include <WiFiManager.h>
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
ScreenMode lastScreen = SCREEN_RADAR;

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
  delay(200);

  display.begin();
  storage.begin();
  display.setStorageStatus(storage.isReady(), storage.statusText());
  display.showSplash();
  delay(600);

  skywatch.begin();
  input.begin();
  display.showSystemCheck(skywatch);
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

  if (!skywatch.coordsWereOverridden()) {
    skywatch.fetchHomeLocationFromIP();
  }

  web.begin(&skywatch);

  skywatch.updateBattery();
  skywatch.updatePlanes();
  logPendingEvents();
  display.drawScreen(input.currentScreen(), skywatch);

  lastPlaneUpdate = millis();
  lastUiRedraw = millis();
  lastBatteryUpdate = millis();
}

void loop() {
  web.handleClient();
  input.update();

  unsigned long now = millis();

  if (now - lastPlaneUpdate >= PLANE_UPDATE_INTERVAL_MS) {
    lastPlaneUpdate = now;
    skywatch.updatePlanes();
    logPendingEvents();
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

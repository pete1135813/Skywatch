#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "SkywatchData.h"
#include "config.h"

class SkywatchStorage;

enum ScreenMode {
  SCREEN_RADAR,
  SCREEN_LIST,
  SCREEN_TRAFFIC,
  SCREEN_SYSTEM
};

class DisplayCYD {
public:
  void begin();
  void setStorageStatus(bool ready, const String &status);
  void setStorage(SkywatchStorage *storage);
  void setListHistoryMode(bool active);
  void showSplash();
  void showSystemCheck(const SkywatchData &data);
  void showConnectingScreen();
  void showPortalInstructions();
  void showStatus(const String &msg);

  void drawScreen(ScreenMode screen, const SkywatchData &data);
  void drawRadar(const SkywatchData &data, bool forceChrome = false);
  void drawAircraftList(const SkywatchData &data);
  void drawTrafficPage(const SkywatchData &data);
  void drawSystemPage(const SkywatchData &data);

  // Free / restore the large radar sprite so HTTPS/TLS has enough heap.
  void pauseHeavyBuffers();
  void resumeHeavyBuffers();
  // Create radar sprite after splash (keeps RAM free for SD logo load).
  void ensureRadarSprite();
  bool splashLogoOk() const { return logoLoadedOk; }

private:
  bool logoLoadedOk = false;
  TFT_eSPI tft = TFT_eSPI();
  TFT_eSprite radarSprite = TFT_eSprite(&tft);
  bool radarSpriteReady = false;
  unsigned long frameCounter = 0;
  unsigned long fpsWindowStart = 0;
  int fps = 0;
  bool sdReady = false;
  String sdStatus = "N/A";
  SkywatchStorage *storage = nullptr;
  bool listHistoryMode = false;
  bool historyLoaded = false;
  unsigned long lastHistoryLoadMs = 0;
  bool pageFullRedraw = true;
  uint32_t lastPageSignature = 0;
  uint32_t lastRadarSignature = 0;
  bool radarBaseDrawn = false;
  float previousSweepAngle = -1.0f;

  // Flicker control: static page content and radar chrome are only repainted
  // when their data changes.
  ScreenMode lastScreen = SCREEN_SYSTEM; // force first paint as "changed"
  unsigned long lastChromeMs = 0;
  bool lastRefreshPending = false;
  bool alertFrozen = false;
  String alertFreezeKey;
  bool menuDrawnForScreen = false;

  void renderRadarSprite(const SkywatchData &data);
  uint32_t contentSignature(ScreenMode screen, const SkywatchData &data) const;
  void drawRadarGrid();
  void drawRadarSweep();
  void erasePreviousRadarSweep();
  void drawPlaneTrail(const Plane &p);
  void drawPlaneTarget(const Plane &p, int index);
  void drawAircraftTriangle(int x, int y, float headingDeg, uint16_t col);
  void radarPoint(float distanceNm, float bearingDeg, int &x, int &y) const;
  void drawBatteryIndicator(float batteryPercent);
  void drawTopBar(const char *title, const SkywatchData &data);
  void drawInfoStrip(const SkywatchData &data);
  void drawTouchMenu(ScreenMode activeScreen);
  void drawListTabs();
  void drawEventHistory(const SkywatchData &data);
  void drawMenuButton(int index, const char *label, bool active);
  void drawLiveStatusPanel(const SkywatchData &data);
  void drawRefreshButton(bool active = false);
  void drawRadarChrome(const SkywatchData &data);
  uint16_t altitudeColor(float altitudeFt);
  uint16_t trailColorForAge(int ageIndex);
  uint16_t threatColor(uint8_t level);
  const char* threatLabel(uint8_t level);
  String etaString(int etaSec);
  bool drawBmpFromSD(const char *filename, int16_t x, int16_t y);
  bool tryDrawSplashLogo();
  void drawAlertBanner(const SkywatchData &data);
  float trafficPriority(const Plane &p);
  void updateFps();
  void paintScreen(ScreenMode screen, const SkywatchData &data, bool forceChrome);
};

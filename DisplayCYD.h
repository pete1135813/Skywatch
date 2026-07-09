#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "SkywatchData.h"
#include "config.h"

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
  void showSplash();
  void showSystemCheck(const SkywatchData &data);
  void showConnectingScreen();
  void showPortalInstructions();
  void showStatus(const String &msg);

  void drawScreen(ScreenMode screen, const SkywatchData &data);
  void drawRadar(const SkywatchData &data);
  void drawAircraftList(const SkywatchData &data);
  void drawTrafficPage(const SkywatchData &data);
  void drawSystemPage(const SkywatchData &data);

private:
  TFT_eSPI tft = TFT_eSPI();
  TFT_eSprite radarSprite = TFT_eSprite(&tft);
  bool radarSpriteReady = false;
  unsigned long frameCounter = 0;
  unsigned long fpsWindowStart = 0;
  int fps = 0;
  bool sdReady = false;
  String sdStatus = "N/A";

  void renderRadarSprite(const SkywatchData &data);
  void drawRadarGrid();
  void drawRadarSweep();
  void drawPlaneTrail(const Plane &p);
  void drawPlaneTarget(const Plane &p, int index);
  void drawAircraftTriangle(int x, int y, float headingDeg, uint16_t col);
  void radarPoint(float distanceNm, float bearingDeg, int &x, int &y) const;
  void drawBatteryIndicator(float batteryPercent);
  void drawTopBar(const char *title, const SkywatchData &data);
  void drawInfoStrip(const SkywatchData &data);
  void drawTouchMenu(ScreenMode activeScreen);
  void drawMenuButton(int index, const char *label, bool active);
  void drawLiveStatusPanel(const SkywatchData &data);
  uint16_t altitudeColor(float altitudeFt);
  uint16_t trailColorForAge(int ageIndex);
  uint16_t threatColor(uint8_t level);
  const char* threatLabel(uint8_t level);
  String etaString(int etaSec);
  bool drawBmpFromSD(const char *filename, int16_t x, int16_t y);
  void drawAlertBanner(const SkywatchData &data);
  float trafficPriority(const Plane &p);
  void updateFps();
};

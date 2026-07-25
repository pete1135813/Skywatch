#include "DisplayCYD.h"
#include <math.h>
#include <string.h>
#include <WiFi.h>
#include <SD.h>
#include "CydSpi.h"
#include "SkywatchStorage.h"

static uint32_t displayHashMix(uint32_t hash, uint32_t value) {
  hash ^= value;
  hash *= 16777619UL;
  return hash;
}

static uint32_t displayHashString(uint32_t hash, const String &value) {
  for (unsigned int i = 0; i < value.length(); i++) {
    hash = displayHashMix(hash, (uint8_t)value.charAt(i));
  }
  return hash;
}

void DisplayCYD::begin() {
  tft.init();
  tft.setRotation(1); // CYD landscape: 320x240
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(1);
  // Build 28.1 uses an incremental direct renderer. Keeping the sprite
  // unallocated avoids full-frame pushes and leaves about 85 KB for TLS/JSON.
  radarSpriteReady = false;
  fpsWindowStart = millis();
  Serial.printf("[Display] TFT ready, free heap %u (sprite deferred for splash)\n",
                ESP.getFreeHeap());
}

void DisplayCYD::ensureRadarSprite() {
  // Compatibility entry point retained for the network heap hooks.
  // The incremental renderer intentionally does not allocate a frame sprite.
  radarSpriteReady = false;
  Serial.printf("[Display] Incremental radar active — free heap %u maxAlloc %u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void DisplayCYD::pauseHeavyBuffers() {
  // No large display buffer is allocated by the incremental renderer.
}

void DisplayCYD::resumeHeavyBuffers() {
  // No large display buffer needs to be restored.
}

void DisplayCYD::setStorageStatus(bool ready, const String &status) {
  sdReady = ready;
  sdStatus = status;
}

void DisplayCYD::setStorage(SkywatchStorage *storageRef) {
  storage = storageRef;
}

void DisplayCYD::setListHistoryMode(bool active) {
  if (listHistoryMode == active) return;
  listHistoryMode = active;
  historyLoaded = false;
  lastHistoryLoadMs = 0;
  menuDrawnForScreen = false;
  pageFullRedraw = true;
  lastPageSignature = 0;
}

bool DisplayCYD::drawBmpFromSD(const char *filename, int16_t x, int16_t y) {
  if (!sdReady) {
    Serial.println(F("[BMP] skipped — SD not ready"));
    return false;
  }
  cydClaimSdSpi();

  if (!SD.exists(filename)) {
    Serial.printf("[BMP] missing: %s\n", filename);
    return false;
  }

  File bmpFS = SD.open(filename, FILE_READ);
  if (!bmpFS) {
    Serial.printf("[BMP] open failed: %s\n", filename);
    return false;
  }

  auto read16 = [&]() -> uint16_t {
    uint16_t result;
    ((uint8_t *)&result)[0] = bmpFS.read();
    ((uint8_t *)&result)[1] = bmpFS.read();
    return result;
  };
  auto read32 = [&]() -> uint32_t {
    uint32_t result;
    ((uint8_t *)&result)[0] = bmpFS.read();
    ((uint8_t *)&result)[1] = bmpFS.read();
    ((uint8_t *)&result)[2] = bmpFS.read();
    ((uint8_t *)&result)[3] = bmpFS.read();
    return result;
  };

  uint16_t magic = read16();
  if (magic != 0x4D42) {
    Serial.printf("[BMP] bad magic 0x%04X in %s\n", magic, filename);
    bmpFS.close();
    return false;
  }
  (void)read32(); // file size
  (void)read32(); // reserved
  uint32_t imageOffset = read32();
  uint32_t dibSize = read32();
  int32_t bmpWidth = (int32_t)read32();
  int32_t bmpHeight = (int32_t)read32();
  if (read16() != 1) { bmpFS.close(); Serial.println(F("[BMP] bad planes")); return false; }
  uint16_t depth = read16();
  uint32_t compression = read32();

  // 24-bit uncompressed is required. (Some editors write 32-bit — reject clearly.)
  if (depth != 24) {
    Serial.printf("[BMP] need 24-bit, got %u bpp (%s)\n", depth, filename);
    bmpFS.close();
    return false;
  }
  if (compression != 0) {
    Serial.printf("[BMP] compressed (%lu) not supported: %s\n",
                  (unsigned long)compression, filename);
    bmpFS.close();
    return false;
  }
  (void)dibSize;

  bool flip = true;
  if (bmpHeight < 0) { bmpHeight = -bmpHeight; flip = false; }
  uint32_t rowSize = (bmpWidth * 3 + 3) & ~3;
  uint8_t sdbuffer[3 * 40];
  uint16_t linebuffer[40];

  int16_t w = bmpWidth;
  int16_t h = bmpHeight;
  if ((x + w - 1) >= tft.width())  w = tft.width()  - x;
  if ((y + h - 1) >= tft.height()) h = tft.height() - y;

  Serial.printf("[BMP] drawing %s (%dx%d) offset=%lu  swapBytes=%d invert=%d swapRB=%d\n",
                filename, (int)bmpWidth, (int)bmpHeight,
                (unsigned long)imageOffset,
                SPLASH_BMP_SWAP_BYTES, SPLASH_BMP_INVERT, SPLASH_BMP_SWAP_RB);

  // Make sure any prior TFT SPI transaction is closed before we stream pixels.
  tft.endWrite();

  // TFT_eSPI: pushImage RGB565 buffers usually need byte-swap for correct colors.
  const bool prevSwap = tft.getSwapBytes();
#if SPLASH_BMP_SWAP_BYTES
  tft.setSwapBytes(true);
#else
  tft.setSwapBytes(false);
#endif

  for (int row = 0; row < h; row++) {
    uint32_t pos = imageOffset + (flip ? (bmpHeight - 1 - row) : row) * rowSize;
    if (!bmpFS.seek(pos)) {
      Serial.printf("[BMP] seek failed at row %d\n", row);
      tft.setSwapBytes(prevSwap);
      bmpFS.close();
      return false;
    }
    int col = 0;
    while (col < w) {
      int chunk = min(40, w - col);
      size_t got = bmpFS.read(sdbuffer, chunk * 3);
      if (got < (size_t)(chunk * 3)) {
        Serial.println(F("[BMP] short read"));
        tft.setSwapBytes(prevSwap);
        bmpFS.close();
        return false;
      }
      for (int i = 0; i < chunk; i++) {
        // Standard Windows BMP 24-bit pixel order is B,G,R.
        uint8_t b = sdbuffer[i * 3 + 0];
        uint8_t g = sdbuffer[i * 3 + 1];
        uint8_t r = sdbuffer[i * 3 + 2];
#if SPLASH_BMP_SWAP_RB
        uint8_t tmp = r; r = b; b = tmp;
#endif
#if SPLASH_BMP_INVERT
        // Photographic negative — each channel flipped.
        r = (uint8_t)(255 - r);
        g = (uint8_t)(255 - g);
        b = (uint8_t)(255 - b);
#endif
        linebuffer[i] = tft.color565(r, g, b);
      }
      tft.pushImage(x + col, y + row, chunk, 1, linebuffer);
      col += chunk;
    }
    // Keep the watchdog happy on large 320x240 logos.
    if ((row & 15) == 0) yield();
  }

  tft.setSwapBytes(prevSwap);
  bmpFS.close();
  Serial.println(F("[BMP] splash logo OK"));
  return true;
}

bool DisplayCYD::tryDrawSplashLogo() {
  if (!sdReady) {
    Serial.println(F("[BMP] SD not ready — cannot load logo"));
    return false;
  }
  cydClaimSdSpi();

  // List /logos so a wrong filename on the card is obvious in Serial.
  Serial.println(F("[BMP] Listing /logos ..."));
  File dir = SD.open("/logos");
  if (!dir) {
    Serial.println(F("[BMP] /logos open failed (folder missing?)"));
  } else if (!dir.isDirectory()) {
    Serial.println(F("[BMP] /logos is not a directory"));
    dir.close();
  } else {
    int n = 0;
    for (;;) {
      File ent = dir.openNextFile();
      if (!ent) break;
      Serial.printf("[BMP]   %s  (%u bytes)\n", ent.name(), (unsigned)ent.size());
      ent.close();
      n++;
      if (n > 20) break;
    }
    dir.close();
    if (n == 0) Serial.println(F("[BMP]   (empty folder)"));
  }

  // Prefer the documented path, then a root fallback.
  if (drawBmpFromSD(SD_LOGO_PATH, 0, 0)) { logoLoadedOk = true; return true; }
  if (drawBmpFromSD(SD_LOGO_PATH_ALT, 0, 0)) { logoLoadedOk = true; return true; }
  // Common 8.3 / case variants on FAT cards formatted by Windows.
  if (drawBmpFromSD("/logos/SKYWATCH.BMP", 0, 0)) { logoLoadedOk = true; return true; }
  if (drawBmpFromSD("/LOGOS/SKYWATCH.BMP", 0, 0)) { logoLoadedOk = true; return true; }
  logoLoadedOk = false;
  return false;
}

void DisplayCYD::showSplash() {
  tft.fillScreen(TFT_BLACK);
  Serial.println(F("[BMP] Splash — attempting SD logo..."));
  Serial.printf("[BMP] sdReady=%d heap=%u\n", sdReady ? 1 : 0, ESP.getFreeHeap());

  // If a 24-bit BMP logo is on the SD card, use it as the splash screen.
  // Otherwise draw a lightweight vector fallback so the firmware still boots cleanly.
  if (tryDrawSplashLogo()) {
    delay(1500);
    return;
  }

  Serial.println(F("[BMP] Logo failed — vector splash fallback"));
  // Vector fallback splash screen.
  int cx = CENTER_X;
  int cy = CENTER_Y - 12;
  tft.drawCircle(cx, cy, 42, TFT_DARKGREEN);
  tft.drawCircle(cx, cy, 25, TFT_DARKGREEN);
  tft.drawLine(cx - 42, cy, cx + 42, cy, TFT_DARKGREEN);
  tft.drawLine(cx, cy - 42, cx, cy + 42, TFT_DARKGREEN);
  tft.drawLine(cx, cy, cx + 32, cy - 28, TFT_GREEN);
  tft.fillTriangle(cx + 36, cy - 32, cx + 28, cy - 28, cx + 32, cy - 20, TFT_CYAN);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  tft.drawString("SKYWATCH", CENTER_X, CENTER_Y + 45);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("MK IV  PERSONAL AIRSPACE OBSERVER", CENTER_X, CENTER_Y + 70);
}

void DisplayCYD::showSystemCheck(const SkywatchData &data) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.drawString("SKYWATCH MK IV", 8, 10);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(BUILD_LABEL, 200, 10);
  tft.drawLine(8, 25, 312, 25, TFT_DARKGREY);

  struct Row { const char *name; String value; uint16_t color; };
  Row rows[] = {
    {"Display", "OK", TFT_GREEN},
    {"Memory", String((int)(ESP.getFreeHeap() / 1024)) + "k", TFT_GREEN},
    {"Touchscreen", ENABLE_TOUCH_NAV ? "OK" : "OFF", ENABLE_TOUCH_NAV ? TFT_GREEN : TFT_DARKGREY},
    {"SD Card", sdStatus, sdReady ? TFT_GREEN : TFT_ORANGE},
    {"WiFi", WiFi.status() == WL_CONNECTED ? "OK" : "WAIT", WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_YELLOW},
    {"Source", data.getActiveSourceLabel(), TFT_CYAN},
    {"Airspace Engine", "OK", TFT_GREEN},
    {"Traffic Engine", "OK", TFT_GREEN},
    {"Alerts Engine", ENABLE_OBSERVER_ALERTS ? "OK" : "OFF", ENABLE_OBSERVER_ALERTS ? TFT_GREEN : TFT_DARKGREY},
  };

  int y = 38;
  for (uint8_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(rows[i].name, 18, y);
    for (int x = 120; x < 226; x += 8) tft.drawString(".", x, y);
    tft.setTextColor(rows[i].color, TFT_BLACK);
    tft.drawString(rows[i].value, 238, y);
    y += 16;
  }

  tft.drawLine(8, 188, 312, 188, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("ALL SYSTEMS GO", CENTER_X, 207);
}

void DisplayCYD::showConnectingScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting to WiFi...", 10, 28);
  tft.drawString("If this is first run or WiFi changed,", 10, 50);
  tft.drawString("connect to setup hotspot:", 10, 66);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("SSID: Skywatch", 10, 88);
}

void DisplayCYD::showPortalInstructions() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Skywatch Setup", 10, 15);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("1) Open WiFi settings", 10, 42);
  tft.drawString("2) Connect to 'Skywatch'", 10, 58);
  tft.drawString("3) Browser should open", 10, 74);
  tft.drawString("   or visit 192.168.4.1", 10, 90);
  tft.drawString("4) Enter WiFi/hotspot credentials", 10, 106);
}

void DisplayCYD::showStatus(const String &msg) {
  tft.fillRect(0, TOUCH_BAR_Y - 14, SCREEN_WIDTH, 14, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(msg, 6, TOUCH_BAR_Y - 7);
}

void DisplayCYD::updateFps() {
  frameCounter++;
  unsigned long now = millis();
  if (now - fpsWindowStart >= 1000) {
    fps = frameCounter;
    frameCounter = 0;
    fpsWindowStart = now;
  }
}

void DisplayCYD::paintScreen(ScreenMode screen, const SkywatchData &data, bool forceChrome) {
  switch (screen) {
    case SCREEN_LIST:    drawAircraftList(data); break;
    case SCREEN_TRAFFIC: drawTrafficPage(data); break;
    case SCREEN_SYSTEM:  drawSystemPage(data); break;
    case SCREEN_RADAR:
    default:             drawRadar(data, forceChrome); break;
  }
}

uint32_t DisplayCYD::contentSignature(ScreenMode screen, const SkywatchData &data) const {
  uint32_t hash = 2166136261UL;
  hash = displayHashMix(hash, (uint32_t)screen);
  hash = displayHashMix(hash, (uint32_t)data.getPlaneCount());
  hash = displayHashMix(hash, (uint32_t)(data.getBatteryPercent() + 0.5f));

  if (screen == SCREEN_LIST && listHistoryMode) {
    // Poll the event file on the configured history cadence. The loader then
    // supplies a more specific status for an empty or malformed file.
    hash = displayHashMix(hash, millis() / EVENT_HISTORY_REFRESH_MS);
    if (storage) {
      hash = displayHashMix(hash, (uint32_t)storage->getHistoryCount());
      hash = displayHashString(hash, storage->getHistoryStatus());
    }
    return hash;
  }

  if (screen == SCREEN_SYSTEM) {
    hash = displayHashString(hash, data.getActiveSourceLabel());
    hash = displayHashString(hash, data.getLocalStatus());
    hash = displayHashString(hash, data.getOpenSkyModeLabel());
    hash = displayHashString(hash, data.getLastApiStatus());
    hash = displayHashString(hash, data.getHomeLocationSource());
    hash = displayHashMix(hash, (uint32_t)data.getLastHttpCode());
    hash = displayHashMix(hash, (uint32_t)data.getEventCount());
    hash = displayHashMix(hash, (uint32_t)(data.getHomeLat() * 100000.0f));
    hash = displayHashMix(hash, (uint32_t)(data.getHomeLon() * 100000.0f));
    unsigned long age = data.getLastPlaneUpdateMs()
        ? (millis() - data.getLastPlaneUpdateMs()) / 5000UL
        : 0;
    hash = displayHashMix(hash, age);
    return hash;
  }

  // Radar, live list and traffic pages only repaint their aircraft layer when
  // an actual displayed value changes, not on every scheduler tick.
  for (int i = 0; i < data.getPlaneCount(); i++) {
    const Plane &p = data.getPlane(i);
    hash = displayHashString(hash, p.icao);
    hash = displayHashString(hash, p.callsign);
    hash = displayHashMix(hash, (uint32_t)(p.distanceNm * 10.0f));
    hash = displayHashMix(hash, (uint32_t)(p.bearingDeg * 10.0f));
    hash = displayHashMix(hash, (uint32_t)(p.headingDeg * 10.0f));
    hash = displayHashMix(hash, (uint32_t)p.altitudeFt);
    hash = displayHashMix(hash, (uint32_t)(p.cpaDistanceNm * 10.0f));
    hash = displayHashMix(hash, (uint32_t)p.cpaEtaSec);
    hash = displayHashMix(hash, (uint32_t)p.threatLevel);
    hash = displayHashMix(hash, p.approaching ? 1U : 0U);
  }
  return hash;
}

void DisplayCYD::drawScreen(ScreenMode screen, const SkywatchData &data) {
  updateFps();

  const bool screenChanged = (screen != lastScreen);
  if (screenChanged) {
    lastScreen = screen;
    menuDrawnForScreen = false;
    lastChromeMs = 0; // force chrome on new page
    pageFullRedraw = true;
    lastPageSignature = 0;
    radarBaseDrawn = false;
    previousSweepAngle = -1.0f;
  }

#if ENABLE_OBSERVER_ALERTS
  const bool alertNow = data.hasRecentAlert(ALERT_POPUP_MS);
  if (alertNow) {
    const SkyEvent &e = data.getLatestEvent();
    String key = e.type + "|" + e.callsign + "|" + String(e.createdMs);
    // Freeze the underlay while an alert is up — stops radar/status FPS flash through the popup.
    if (!alertFrozen || key != alertFreezeKey) {
      pageFullRedraw = true;
      paintScreen(screen, data, true);
      pageFullRedraw = false;
      drawAlertBanner(data);
      alertFrozen = true;
      alertFreezeKey = key;
    }
    return;
  }

  if (alertFrozen) {
    // Alert just expired — force one full clean redraw underneath.
    alertFrozen = false;
    alertFreezeKey = "";
    lastChromeMs = 0;
    menuDrawnForScreen = false;
    pageFullRedraw = true;
    paintScreen(screen, data, true);
    pageFullRedraw = false;
    return;
  }
#endif

  if (screen != SCREEN_RADAR) {
    const uint32_t signature = contentSignature(screen, data);
    if (!screenChanged && !pageFullRedraw && signature == lastPageSignature) {
      return;
    }
    lastPageSignature = signature;
  }

  paintScreen(screen, data, screenChanged);
  pageFullRedraw = false;
}

void DisplayCYD::drawTopBar(const char *title, const SkywatchData &data) {
  tft.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_H, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(title, 6, 10);
  drawBatteryIndicator(data.getBatteryPercent());
}

void DisplayCYD::drawInfoStrip(const SkywatchData &data) {
  tft.fillRect(0, TOUCH_BAR_Y - 16, SCREEN_WIDTH, 16, TFT_BLACK);
  tft.drawLine(0, TOUCH_BAR_Y - 16, SCREEN_WIDTH, TOUCH_BAR_Y - 16, TFT_DARKGREY);

  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  char buf[96];
  snprintf(buf, sizeof(buf), "Lat %.4f  Lon %.4f  Planes %d",
           data.getHomeLat(), data.getHomeLon(), data.getPlaneCount());
  tft.drawString(buf, 6, TOUCH_BAR_Y - 8);
}

void DisplayCYD::drawMenuButton(int index, const char *label, bool active) {
  int w = SCREEN_WIDTH / 4;
  int x = index * w;
  int y = TOUCH_BAR_Y;

  uint16_t outline = active ? TFT_CYAN : TFT_DARKGREY;
  uint16_t text    = active ? TFT_BLACK : TFT_CYAN;
  uint16_t fill    = active ? TFT_CYAN : TFT_BLACK;

  tft.fillRect(x, y, w, TOUCH_BAR_H, fill);
  tft.drawRect(x, y, w, TOUCH_BAR_H, outline);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(text, fill);
  tft.drawString(label, x + w / 2, y + TOUCH_BAR_H / 2);
}

void DisplayCYD::drawTouchMenu(ScreenMode activeScreen) {
  tft.drawLine(0, TOUCH_BAR_Y, SCREEN_WIDTH, TOUCH_BAR_Y, TFT_CYAN);
  drawMenuButton(0, "RADAR",   activeScreen == SCREEN_RADAR);
  drawMenuButton(1, "LIST",    activeScreen == SCREEN_LIST);
  drawMenuButton(2, "TRAFFIC", activeScreen == SCREEN_TRAFFIC);
  drawMenuButton(3, "SYSTEM",  activeScreen == SCREEN_SYSTEM);
}

void DisplayCYD::drawRadarChrome(const SkywatchData &data) {
  drawBatteryIndicator(data.getBatteryPercent());
  drawLiveStatusPanel(data);
  drawInfoStrip(data);
  bool pending = data.getLastApiStatus().startsWith("Manual refresh");
  drawRefreshButton(pending);
  lastRefreshPending = pending;
  drawTouchMenu(SCREEN_RADAR);
  menuDrawnForScreen = true;
  lastChromeMs = millis();
}

void DisplayCYD::drawRadar(const SkywatchData &data, bool forceChrome) {
  const uint32_t signature = contentSignature(SCREEN_RADAR, data);
  const bool dataChanged = forceChrome || !radarBaseDrawn ||
                           signature != lastRadarSignature;

  if (dataChanged) {
    // A changed traffic set needs one clean background repaint. Sweep-only
    // frames below never clear the scope, which removes the visible flash.
    tft.fillRect(0, 0, STATUS_PANEL_X, TOUCH_BAR_Y - 16, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);
    tft.drawString("SKYWATCH RADAR", 6, 10);
    drawRadarGrid();
    if (ENABLE_MOTION_TRAILS) {
      for (int i = 0; i < data.getPlaneCount(); i++) drawPlaneTrail(data.getPlane(i));
    }
    for (int i = 0; i < data.getPlaneCount(); i++) drawPlaneTarget(data.getPlane(i), i);
    lastRadarSignature = signature;
    radarBaseDrawn = true;
    previousSweepAngle = -1.0f;
  } else if (ENABLE_RADAR_SWEEP) {
    // Erase only the two previous arms, then restore any grid/target pixels
    // underneath. No full-scope clear or bitmap transfer occurs.
    erasePreviousRadarSweep();
    drawRadarGrid();
    if (ENABLE_MOTION_TRAILS) {
      for (int i = 0; i < data.getPlaneCount(); i++) drawPlaneTrail(data.getPlane(i));
    }
    for (int i = 0; i < data.getPlaneCount(); i++) drawPlaneTarget(data.getPlane(i), i);
  }
  if (ENABLE_RADAR_SWEEP) drawRadarSweep();

  // Status / RFRSH / menu are NOT redrawn at radar FPS — keeps the right column steady.
  // RFRSH is outside the scope, so the sweep cannot erase it.
  const bool pending = data.getLastApiStatus().startsWith("Manual refresh");
  const bool pendingChanged = (pending != lastRefreshPending);
  const bool chromeDue = forceChrome || !menuDrawnForScreen ||
                         pendingChanged ||
                         (millis() - lastChromeMs >= PAGE_REDRAW_INTERVAL_MS);
  if (chromeDue) {
    drawRadarChrome(data);
  }
}

void DisplayCYD::drawRefreshButton(bool active) {
  const int x = REFRESH_BTN_X;
  const int y = REFRESH_BTN_Y;
  const int w = REFRESH_BTN_W;
  const int h = REFRESH_BTN_H;

  uint16_t fill    = active ? TFT_YELLOW : TFT_BLACK;
  uint16_t outline = active ? TFT_YELLOW : TFT_CYAN;
  uint16_t text    = active ? TFT_BLACK : TFT_CYAN;

  tft.fillRoundRect(x, y, w, h, 3, fill);
  tft.drawRoundRect(x, y, w, h, 3, outline);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(text, fill);
  tft.drawString(active ? "...." : "RFRSH", x + w / 2, y + h / 2);
}

void DisplayCYD::renderRadarSprite(const SkywatchData &data) {
  radarSprite.fillSprite(TFT_BLACK);
  radarSprite.setTextFont(1);
  radarSprite.setTextDatum(ML_DATUM);
  radarSprite.setTextSize(1);
  radarSprite.setTextColor(TFT_CYAN, TFT_BLACK);
  radarSprite.drawString("SKYWATCH RADAR", 6, 10);

  // Grid
  radarSprite.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS, TFT_DARKGREEN);
  radarSprite.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS * 3 / 4, TFT_DARKGREEN);
  radarSprite.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS / 2, TFT_DARKGREEN);
  radarSprite.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS / 4, TFT_DARKGREEN);
  radarSprite.drawLine(RADAR_CENTER_X - RADAR_RADIUS, RADAR_CENTER_Y, RADAR_CENTER_X + RADAR_RADIUS, RADAR_CENTER_Y, TFT_DARKGREEN);
  radarSprite.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y - RADAR_RADIUS, RADAR_CENTER_X, RADAR_CENTER_Y + RADAR_RADIUS, TFT_DARKGREEN);
  radarSprite.fillCircle(RADAR_CENTER_X, RADAR_CENTER_Y, 3, TFT_GREEN);

  radarSprite.setTextDatum(MC_DATUM);
  radarSprite.setTextColor(TFT_GREEN, TFT_BLACK);
  radarSprite.drawString("N", RADAR_CENTER_X, RADAR_CENTER_Y - RADAR_RADIUS - 9);
  radarSprite.drawString("S", RADAR_CENTER_X, RADAR_CENTER_Y + RADAR_RADIUS + 9);
  radarSprite.drawString("W", RADAR_CENTER_X - RADAR_RADIUS - 10, RADAR_CENTER_Y);
  radarSprite.drawString("E", RADAR_CENTER_X + RADAR_RADIUS + 10, RADAR_CENTER_Y);
  radarSprite.setTextDatum(ML_DATUM);
  radarSprite.setTextColor(TFT_DARKGREEN, TFT_BLACK);
  radarSprite.drawString("5",  RADAR_CENTER_X + RADAR_RADIUS / 4 + 3, RADAR_CENTER_Y - 8);
  radarSprite.drawString("10", RADAR_CENTER_X + RADAR_RADIUS / 2 + 3, RADAR_CENTER_Y - 8);
  radarSprite.drawString("15", RADAR_CENTER_X + RADAR_RADIUS * 3 / 4 + 3, RADAR_CENTER_Y - 8);
  radarSprite.drawString("20nm", RADAR_CENTER_X + RADAR_RADIUS - 20, RADAR_CENTER_Y - 8);

  // Sweep
  if (ENABLE_RADAR_SWEEP) {
    float angleDeg = fmodf((millis() / 18.0f), 360.0f);
    float a = angleDeg * PI / 180.0f;
    int x2 = RADAR_CENTER_X + (int)(RADAR_RADIUS * sin(a));
    int y2 = RADAR_CENTER_Y - (int)(RADAR_RADIUS * cos(a));
    radarSprite.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y, x2, y2, TFT_GREEN);
    float a2 = (angleDeg - 8.0f) * PI / 180.0f;
    int x3 = RADAR_CENTER_X + (int)((RADAR_RADIUS - 4) * sin(a2));
    int y3 = RADAR_CENTER_Y - (int)((RADAR_RADIUS - 4) * cos(a2));
    radarSprite.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y, x3, y3, TFT_DARKGREEN);
  }

  // Trails
  if (ENABLE_MOTION_TRAILS) {
    for (int n = 0; n < data.getPlaneCount(); n++) {
      const Plane &p = data.getPlane(n);
      if (!p.valid) continue;
      for (int i = 0; i < TRAIL_POINTS; i++) {
        if (!p.trail[i].valid) continue;
        int x, y;
        radarPoint(p.trail[i].distanceNm, p.trail[i].bearingDeg, x, y);
        uint16_t col = trailColorForAge(i);
        int radius = (i >= TRAIL_POINTS - 2) ? 2 : 1;
        radarSprite.fillCircle(x, y, radius, col);
      }
    }
  }

  // Aircraft + labels
  for (int index = 0; index < data.getPlaneCount(); index++) {
    const Plane &p = data.getPlane(index);
    if (!p.valid) continue;
    int x, y;
    radarPoint(p.distanceNm, p.bearingDeg, x, y);
    uint16_t col = altitudeColor(p.altitudeFt);

    if (ENABLE_HEADING_TRIANGLES) {
      float a = p.headingDeg * PI / 180.0f;
      float left = a + 2.45f;
      float right = a - 2.45f;
      int noseX = x + (int)(8 * sin(a));
      int noseY = y - (int)(8 * cos(a));
      int lX = x + (int)(5 * sin(left));
      int lY = y - (int)(5 * cos(left));
      int rX = x + (int)(5 * sin(right));
      int rY = y - (int)(5 * cos(right));
      radarSprite.fillTriangle(noseX, noseY, lX, lY, rX, rY, col);
      radarSprite.drawLine(x, y, noseX, noseY, TFT_WHITE);
    } else {
      radarSprite.fillCircle(x, y, 3, col);
    }

    String label = p.callsign.length() ? p.callsign : String("AC") + index;
    label.trim();
    if (label.length() > 7) label = label.substring(0, 7);
    int labelX = x + 8;
    int labelY = y - 2;
    if (ENABLE_SMART_LABELS) {
      labelY += (index % 4) * 8;
      if (labelY > TOUCH_BAR_Y - 38) labelY = TOUCH_BAR_Y - 38 - (index % 3) * 8;
      if (labelY < TOP_BAR_H + 6) labelY = TOP_BAR_H + 6 + (index % 3) * 8;
      if (labelX > STATUS_PANEL_X - 52) labelX = STATUS_PANEL_X - 52;
      if (labelX < 2) labelX = 2;
    }
    radarSprite.setTextDatum(ML_DATUM);
    radarSprite.setTextSize(1);
    radarSprite.setTextColor(col, TFT_BLACK);
    radarSprite.drawString(label, labelX, labelY);
    radarSprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    radarSprite.drawString(String((int)p.distanceNm) + "nm", labelX, labelY + 9);
  }

  radarSprite.pushSprite(0, 0);
}

void DisplayCYD::drawLiveStatusPanel(const SkywatchData &data) {
  int x = STATUS_PANEL_X;
  int y = 27;
  int w = SCREEN_WIDTH - STATUS_PANEL_X - 5;
  // Leave room under the panel for the RFRSH button (same column, outside sprite).
  int h = REFRESH_BTN_Y - y - 4;

  tft.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_BLACK);
  tft.drawRoundRect(x, y, w, h, 4, TFT_DARKGREEN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("LIVE", x + 6, y + 5);

  int yy = y + 22;
  auto row = [&](const String &k, const String &v, uint16_t col) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(k, x + 6, yy);
    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(v, x + 57, yy);
    yy += 14;
  };

  row("Planes", String(data.getPlaneCount()), TFT_GREEN);
  row("Source", data.getActiveSourceLabel(), TFT_CYAN);
  row("Range", String((int)MAX_RANGE_NM) + "nm", TFT_GREEN);
  row("WiFi", WiFi.status() == WL_CONNECTED ? "OK" : "NO", WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_RED);
  row("RSSI", WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : "--", TFT_GREEN);
  row("HTTP", String(data.getLastHttpCode()), data.getLastHttpCode() == 200 ? TFT_GREEN : TFT_YELLOW);

  unsigned long age = data.getLastPlaneUpdateMs() ? (millis() - data.getLastPlaneUpdateMs()) / 1000 : 0;
  row("Age", String(age) + "s", TFT_GREEN);
  row("FPS", String(fps), TFT_GREEN);
  row("RAM", String((int)(ESP.getFreeHeap() / 1024)) + "k", TFT_GREEN);
  row("OSky", data.getOpenSkyModeLabel(), TFT_CYAN);
  row("CPA", "ON", TFT_GREEN);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  String status = data.getLastApiStatus();
  if (status.length() > 15) status = status.substring(0, 15);
  tft.drawString(status, x + 6, y + h - 13);
}

void DisplayCYD::drawRadarGrid() {
  tft.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS, TFT_DARKGREEN);
  tft.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS * 3 / 4, TFT_DARKGREEN);
  tft.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS / 2, TFT_DARKGREEN);
  tft.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS / 4, TFT_DARKGREEN);

  tft.drawLine(RADAR_CENTER_X - RADAR_RADIUS, RADAR_CENTER_Y, RADAR_CENTER_X + RADAR_RADIUS, RADAR_CENTER_Y, TFT_DARKGREEN);
  tft.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y - RADAR_RADIUS, RADAR_CENTER_X, RADAR_CENTER_Y + RADAR_RADIUS, TFT_DARKGREEN);

  tft.fillCircle(RADAR_CENTER_X, RADAR_CENTER_Y, 3, TFT_GREEN);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("N", RADAR_CENTER_X, RADAR_CENTER_Y - RADAR_RADIUS - 9);
  tft.drawString("S", RADAR_CENTER_X, RADAR_CENTER_Y + RADAR_RADIUS + 9);
  tft.drawString("W", RADAR_CENTER_X - RADAR_RADIUS - 10, RADAR_CENTER_Y);
  tft.drawString("E", RADAR_CENTER_X + RADAR_RADIUS + 10, RADAR_CENTER_Y);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_DARKGREEN);
  tft.drawString("5",  RADAR_CENTER_X + RADAR_RADIUS / 4 + 3, RADAR_CENTER_Y - 8);
  tft.drawString("10", RADAR_CENTER_X + RADAR_RADIUS / 2 + 3, RADAR_CENTER_Y - 8);
  tft.drawString("15", RADAR_CENTER_X + RADAR_RADIUS * 3 / 4 + 3, RADAR_CENTER_Y - 8);
  tft.drawString("20nm", RADAR_CENTER_X + RADAR_RADIUS - 20, RADAR_CENTER_Y - 8);
}

void DisplayCYD::erasePreviousRadarSweep() {
  if (previousSweepAngle < 0.0f) return;

  float a = previousSweepAngle * PI / 180.0f;
  int x2 = RADAR_CENTER_X + (int)(RADAR_RADIUS * sin(a));
  int y2 = RADAR_CENTER_Y - (int)(RADAR_RADIUS * cos(a));
  tft.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y, x2, y2, TFT_BLACK);

  float a2 = (previousSweepAngle - 8.0f) * PI / 180.0f;
  int x3 = RADAR_CENTER_X + (int)((RADAR_RADIUS - 4) * sin(a2));
  int y3 = RADAR_CENTER_Y - (int)((RADAR_RADIUS - 4) * cos(a2));
  tft.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y, x3, y3, TFT_BLACK);
}

void DisplayCYD::drawRadarSweep() {
  float angleDeg = fmodf((millis() / 18.0f), 360.0f);  // about 6.5 sec per revolution
  float a = angleDeg * PI / 180.0f;
  int x2 = RADAR_CENTER_X + (int)(RADAR_RADIUS * sin(a));
  int y2 = RADAR_CENTER_Y - (int)(RADAR_RADIUS * cos(a));
  tft.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y, x2, y2, TFT_GREEN);

  // small dim afterglow arm trailing behind the sweep
  float a2 = (angleDeg - 8.0f) * PI / 180.0f;
  int x3 = RADAR_CENTER_X + (int)((RADAR_RADIUS - 4) * sin(a2));
  int y3 = RADAR_CENTER_Y - (int)((RADAR_RADIUS - 4) * cos(a2));
  tft.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y, x3, y3, TFT_DARKGREEN);
  previousSweepAngle = angleDeg;
}

uint16_t DisplayCYD::altitudeColor(float altitudeFt) {
  if (altitudeFt < 5000) return TFT_GREEN;
  if (altitudeFt < 15000) return TFT_YELLOW;
  if (altitudeFt < 30000) return TFT_ORANGE;
  if (altitudeFt < 40000) return TFT_CYAN;
  return TFT_MAGENTA;
}

uint16_t DisplayCYD::trailColorForAge(int ageIndex) {
  // ageIndex 0 = oldest, TRAIL_POINTS-1 = newest
  if (ageIndex >= TRAIL_POINTS - 2) return TFT_GREEN;
  if (ageIndex >= TRAIL_POINTS - 5) return TFT_DARKGREEN;
  return TFT_DARKGREY;
}

void DisplayCYD::radarPoint(float distanceNm, float bearingDeg, int &x, int &y) const {
  float r = (distanceNm / MAX_RANGE_NM) * RADAR_RADIUS;
  if (r > RADAR_RADIUS) r = RADAR_RADIUS;
  float bearingRad = bearingDeg * PI / 180.0f;
  x = RADAR_CENTER_X + (int)(r * sin(bearingRad));
  y = RADAR_CENTER_Y - (int)(r * cos(bearingRad));
}

void DisplayCYD::drawPlaneTrail(const Plane &p) {
  if (!p.valid) return;

  for (int i = 0; i < TRAIL_POINTS; i++) {
    if (!p.trail[i].valid) continue;

    int x, y;
    radarPoint(p.trail[i].distanceNm, p.trail[i].bearingDeg, x, y);

    uint16_t col = trailColorForAge(i);
    int radius = (i >= TRAIL_POINTS - 2) ? 2 : 1;
    tft.fillCircle(x, y, radius, col);
  }
}

void DisplayCYD::drawAircraftTriangle(int x, int y, float headingDeg, uint16_t col) {
  float a = headingDeg * PI / 180.0f;
  float left = a + 2.45f;
  float right = a - 2.45f;

  int noseX = x + (int)(8 * sin(a));
  int noseY = y - (int)(8 * cos(a));
  int lX = x + (int)(5 * sin(left));
  int lY = y - (int)(5 * cos(left));
  int rX = x + (int)(5 * sin(right));
  int rY = y - (int)(5 * cos(right));

  tft.fillTriangle(noseX, noseY, lX, lY, rX, rY, col);
  tft.drawLine(x, y, noseX, noseY, TFT_WHITE);
}

void DisplayCYD::drawPlaneTarget(const Plane &p, int index) {
  if (!p.valid) return;

  int x, y;
  radarPoint(p.distanceNm, p.bearingDeg, x, y);
  uint16_t col = altitudeColor(p.altitudeFt);

  if (ENABLE_HEADING_TRIANGLES) drawAircraftTriangle(x, y, p.headingDeg, col);
  else tft.fillCircle(x, y, 3, col);

  String label = p.callsign.length() ? p.callsign : String("AC") + index;
  label.trim();
  if (label.length() > 7) label = label.substring(0, 7);

  int labelX = x + 8;
  int labelY = y - 2;

  if (ENABLE_SMART_LABELS) {
    labelY += (index % 4) * 8;
    if (labelY > TOUCH_BAR_Y - 38) labelY = TOUCH_BAR_Y - 38 - (index % 3) * 8;
    if (labelY < TOP_BAR_H + 6) labelY = TOP_BAR_H + 6 + (index % 3) * 8;
    if (labelX > STATUS_PANEL_X - 52) labelX = STATUS_PANEL_X - 52;
    if (labelX < 2) labelX = 2;
  }

  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(col);
  tft.drawString(label, labelX, labelY);

  tft.setTextColor(TFT_DARKGREY);
  tft.drawString(String((int)p.distanceNm) + "nm", labelX, labelY + 9);
}

void DisplayCYD::drawBatteryIndicator(float batteryPercent) {
  int x = SCREEN_WIDTH - 42;
  int y = 5;
  int w = 30;
  int h = 10;

  tft.drawRect(x, y, w, h, TFT_WHITE);
  tft.drawRect(x + w, y + 2, 3, h - 4, TFT_WHITE);

  int fillWidth = (int)((w - 2) * (batteryPercent / 100.0f));
  fillWidth = constrain(fillWidth, 0, w - 2);

  uint16_t col = TFT_GREEN;
  if (batteryPercent < 20) col = TFT_RED;
  else if (batteryPercent < 50) col = TFT_YELLOW;

  tft.fillRect(x + 1, y + 1, fillWidth, h - 2, col);

  tft.setTextDatum(TR_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", (int)batteryPercent);
  tft.drawString(buf, x - 3, y + h - 1);
}


uint16_t DisplayCYD::threatColor(uint8_t level) {
  if (level >= 3) return TFT_RED;
  if (level == 2) return TFT_ORANGE;
  if (level == 1) return TFT_YELLOW;
  return TFT_GREEN;
}

const char* DisplayCYD::threatLabel(uint8_t level) {
  if (level >= 3) return "HIGH";
  if (level == 2) return "MED";
  if (level == 1) return "LOW";
  return "NONE";
}

String DisplayCYD::etaString(int etaSec) {
  if (etaSec < 0) return "--";
  if (etaSec < 60) return String(etaSec) + "s";
  return String(etaSec / 60) + "m" + String(etaSec % 60) + "s";
}

float DisplayCYD::trafficPriority(const Plane &p) {
  // Bigger number = more important. Threat first, then lower CPA, then closer range.
  float score = p.threatLevel * 1000.0f;
  if (p.approaching && p.cpaEtaSec >= 0) {
    score += 250.0f;
    score += max(0.0f, 100.0f - p.cpaDistanceNm * 8.0f);
    score += max(0.0f, 120.0f - p.cpaEtaSec / 5.0f);
    score += min(120.0f, max(0.0f, p.closingRateKt));
  }
  score += max(0.0f, 40.0f - p.distanceNm);
  return score;
}

void DisplayCYD::drawAlertBanner(const SkywatchData &data) {
#if ENABLE_OBSERVER_ALERTS
  const SkyEvent &e = data.getLatestEvent();
  if (e.type.length() == 0) return;
  uint16_t col = threatColor(e.threatLevel);
  if (e.type == "NEW_TARGET") col = TFT_YELLOW;
  if (e.type == "APPROACHING") col = TFT_ORANGE;

  int x = 36, y = 42, w = 248, h = 120;
  tft.fillRoundRect(x, y, w, h, 8, TFT_BLACK);
  tft.drawRoundRect(x, y, w, h, 8, col);
  tft.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 8, col);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(col, TFT_BLACK);
  String title = e.type;
  title.replace("_", " ");
  tft.drawString(title, x + w / 2, y + 18);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(e.callsign.length() ? e.callsign : "UNKNOWN", x + w / 2, y + 47);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(String(e.distanceNm, 1) + " nm", x + w / 2, y + 72);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("CPA " + String(e.cpaNm, 1) + " nm   ETA " + etaString(e.etaSec), x + w / 2, y + 92);
  tft.setTextColor(col, TFT_BLACK);
  tft.drawString(String("THREAT ") + threatLabel(e.threatLevel), x + w / 2, y + 108);
#endif
}

void DisplayCYD::drawListTabs() {
  const int gap = 4;
  const int tabWidth = (SCREEN_WIDTH - gap * 3) / 2;
  const int leftX = gap;
  const int rightX = leftX + tabWidth + gap;

  auto drawTab = [&](int x, const char *label, bool active) {
    uint16_t fill = active ? TFT_CYAN : TFT_BLACK;
    uint16_t text = active ? TFT_BLACK : TFT_CYAN;
    tft.fillRoundRect(x, LIST_TAB_Y, tabWidth, LIST_TAB_H, 3, fill);
    tft.drawRoundRect(x, LIST_TAB_Y, tabWidth, LIST_TAB_H, 3, TFT_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(text, fill);
    tft.drawString(label, x + tabWidth / 2, LIST_TAB_Y + LIST_TAB_H / 2);
  };

  drawTab(leftX, "LIVE AIRCRAFT", !listHistoryMode);
  drawTab(rightX, "HISTORY", listHistoryMode);
}

void DisplayCYD::drawEventHistory(const SkywatchData &data) {
  const unsigned long now = millis();
  if (storage && (!historyLoaded ||
      now - lastHistoryLoadMs >= EVENT_HISTORY_REFRESH_MS)) {
    storage->loadRecentEvents();
    historyLoaded = true;
    lastHistoryLoadMs = now;
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("TIME", 6, 52);
  tft.drawString("EVENT", 50, 52);
  tft.drawString("CALL", 128, 52);
  tft.drawString("DIST", 198, 52);
  tft.drawString("CPA", 236, 52);
  tft.drawString("THR", 278, 52);
  tft.drawLine(0, 65, SCREEN_WIDTH, 65, TFT_DARKGREEN);

  int count = storage ? storage->getHistoryCount() : 0;
  const int visibleRows = 8;
  for (int i = 0; i < count && i < visibleRows; i++) {
    const StoredEvent &event = storage->getHistoryEvent(i);
    int y = 72 + i * 15;

    String timeLabel = event.timestamp;
    if (timeLabel.length() >= 16 && timeLabel.charAt(10) == ' ') {
      timeLabel = timeLabel.substring(11, 16);
    } else if (timeLabel.length() > 7) {
      timeLabel = timeLabel.substring(0, 7);
    }

    String eventLabel = event.type;
    if (eventLabel == "NEW_TARGET") eventLabel = "NEW";
    else if (eventLabel == "APPROACHING") eventLabel = "APPROACH";
    else if (eventLabel == "THREAT_UP") eventLabel = "THREAT UP";
    if (eventLabel.length() > 10) eventLabel = eventLabel.substring(0, 10);

    String callsign = event.callsign;
    if (callsign.length() > 8) callsign = callsign.substring(0, 8);

    uint16_t color = threatColor(event.threatLevel);
    if (strcmp(event.type, "NEW_TARGET") == 0) color = TFT_YELLOW;
    else if (strcmp(event.type, "APPROACHING") == 0) color = TFT_ORANGE;

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(timeLabel, 6, y);
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(eventLabel, 50, y);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(callsign, 128, y);
    tft.drawString(String(event.distanceNm, 1), 198, y);
    tft.drawString(String(event.cpaNm, 1), 236, y);
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(threatLabel(event.threatLevel), 278, y);
  }

  if (!sdReady) {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("SD unavailable - event history disabled", 50, 112);
  } else if (count == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    String detail = storage ? storage->getHistoryStatus() : "Storage not connected";
    if (detail.length() > 38) detail = detail.substring(0, 38);
    tft.drawString(detail, 64, 112);
  }

  drawInfoStrip(data);
  if (!menuDrawnForScreen) {
    drawTouchMenu(SCREEN_LIST);
    menuDrawnForScreen = true;
  }
}

void DisplayCYD::drawAircraftList(const SkywatchData &data) {
  if (pageFullRedraw) {
    tft.fillRect(0, 0, SCREEN_WIDTH, TOUCH_BAR_Y, TFT_BLACK);
    drawTopBar("SKYWATCH LIST", data);
    drawListTabs();
  } else {
    // Keep the title, tabs and menu steady; refresh only the table body.
    tft.fillRect(0, 48, SCREEN_WIDTH, TOUCH_BAR_Y - 16 - 48, TFT_BLACK);
    drawBatteryIndicator(data.getBatteryPercent());
  }

  if (listHistoryMode) {
    drawEventHistory(data);
    return;
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("CALL", 6, 52);
  tft.drawString("DIST", 70, 52);
  tft.drawString("DIR", 108, 52);
  tft.drawString("CPA", 142, 52);
  tft.drawString("ETA", 182, 52);
  tft.drawString("THR", 224, 52);
  tft.drawString("ALT", 262, 52);
  tft.drawLine(0, 65, SCREEN_WIDTH, 65, TFT_DARKGREEN);

  for (int i = 0; i < data.getPlaneCount(); i++) {
    const Plane &p = data.getPlane(i);
    int y = 72 + i * 15;
    if (y > TOUCH_BAR_Y - 24) break;
    uint16_t col = altitudeColor(p.altitudeFt);
    String call = p.callsign.length() ? p.callsign : p.icao;
    call.trim();
    if (call.length() > 7) call = call.substring(0, 7);

    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(call, 6, y);
    tft.drawString(String(p.distanceNm, 1), 70, y);
    tft.setTextColor(p.approaching ? TFT_ORANGE : TFT_DARKGREY, TFT_BLACK);
    tft.drawString(p.approaching ? "IN" : "OUT", 108, y);
    tft.setTextColor(threatColor(p.threatLevel), TFT_BLACK);
    tft.drawString(String(p.cpaDistanceNm, 1), 142, y);
    tft.drawString(etaString(p.cpaEtaSec), 182, y);
    tft.drawString(threatLabel(p.threatLevel), 224, y);
    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(String((int)p.altitudeFt), 262, y);
  }

  if (data.getPlaneCount() == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("No aircraft in range", 86, 120);
  }

  drawInfoStrip(data);
  if (!menuDrawnForScreen) {
    drawTouchMenu(SCREEN_LIST);
    menuDrawnForScreen = true;
  }
}

void DisplayCYD::drawTrafficPage(const SkywatchData &data) {
  if (pageFullRedraw) {
    tft.fillRect(0, 0, SCREEN_WIDTH, TOUCH_BAR_Y, TFT_BLACK);
    drawTopBar("TRAFFIC / CPA", data);
  } else {
    tft.fillRect(0, TOP_BAR_H, SCREEN_WIDTH,
                 TOUCH_BAR_Y - 16 - TOP_BAR_H, TFT_BLACK);
    drawBatteryIndicator(data.getBatteryPercent());
  }

  int count = data.getPlaneCount();
  int order[MAX_PLANES];
  for (int i = 0; i < count; i++) order[i] = i;

  // Sort by traffic importance: threat, CPA, ETA, closing rate, then distance.
  for (int a = 0; a < count - 1; a++) {
    for (int b = a + 1; b < count; b++) {
      if (trafficPriority(data.getPlane(order[b])) > trafficPriority(data.getPlane(order[a]))) {
        int tmp = order[a]; order[a] = order[b]; order[b] = tmp;
      }
    }
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  if (count == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("No traffic in range", 94, 100);
    drawInfoStrip(data);
    if (!menuDrawnForScreen) {
      drawTouchMenu(SCREEN_TRAFFIC);
      menuDrawnForScreen = true;
    }
    return;
  }

  const Plane &top = data.getPlane(order[0]);
  uint16_t ncol = altitudeColor(top.altitudeFt);
  uint16_t tcol = threatColor(top.threatLevel);
  String ncall = top.callsign.length() ? top.callsign : top.icao;
  ncall.trim();
  if (ncall.length() == 0) ncall = "UNKNOWN";

  tft.drawRoundRect(6, 28, 138, 86, 4, tcol);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("TOP TRAFFIC", 14, 36);
  tft.setTextColor(ncol, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString(ncall.substring(0, 7), 14, 52);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(String(top.distanceNm, 1) + " nm now", 14, 78);
  tft.setTextColor(top.approaching ? TFT_ORANGE : TFT_DARKGREY, TFT_BLACK);
  tft.drawString(top.approaching ? "APPROACHING" : "OPENING / PASSING", 14, 92);
  tft.setTextColor(tcol, TFT_BLACK);
  tft.drawString(String("THREAT ") + threatLabel(top.threatLevel), 14, 104);

  tft.drawRoundRect(154, 28, 160, 86, 4, TFT_DARKGREEN);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("CLOSEST APPROACH", 162, 36);
  tft.setTextColor(tcol, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString(String(top.cpaDistanceNm, 1) + "nm", 162, 54);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("ETA " + etaString(top.cpaEtaSec), 162, 78);
  tft.drawString("Closing " + String((int)top.closingRateKt) + " kt", 162, 92);
  tft.drawString("BRG " + String((int)top.bearingDeg) + "  HDG " + String((int)top.headingDeg), 162, 104);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("# CALL    NOW  CPA  ETA   THR", 8, 124);
  tft.drawLine(0, 138, SCREEN_WIDTH, 138, TFT_DARKGREEN);

  int rows = min(count, 4);
  for (int r = 0; r < rows; r++) {
    const Plane &p = data.getPlane(order[r]);
    String call = p.callsign.length() ? p.callsign : p.icao;
    call.trim();
    if (call.length() > 7) call = call.substring(0, 7);
    int y = 146 + r * 14;
    uint16_t col = threatColor(p.threatLevel);
    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(String(r + 1), 8, y);
    tft.drawString(call, 26, y);
    tft.drawString(String(p.distanceNm, 1), 88, y);
    tft.drawString(String(p.cpaDistanceNm, 1), 128, y);
    tft.drawString(etaString(p.cpaEtaSec), 170, y);
    tft.drawString(threatLabel(p.threatLevel), 224, y);
    tft.setTextColor(p.approaching ? TFT_ORANGE : TFT_DARKGREY, TFT_BLACK);
    tft.drawString(p.approaching ? "IN" : "OUT", 270, y);
  }

  drawInfoStrip(data);
  if (!menuDrawnForScreen) {
    drawTouchMenu(SCREEN_TRAFFIC);
    menuDrawnForScreen = true;
  }
}

void DisplayCYD::drawSystemPage(const SkywatchData &data) {
  if (pageFullRedraw) {
    tft.fillRect(0, 0, SCREEN_WIDTH, TOUCH_BAR_Y, TFT_BLACK);
    drawTopBar("SYSTEM STATUS", data);
  } else {
    tft.fillRect(0, TOP_BAR_H, SCREEN_WIDTH, TOUCH_BAR_Y - TOP_BAR_H, TFT_BLACK);
    drawBatteryIndicator(data.getBatteryPercent());
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.drawString("Display: CYD 320x240 landscape", 8, 28);
  tft.drawString("Data source: " + data.getActiveSourceLabel(), 8, 45);
  tft.drawString("Local ADS-B: " + data.getLocalStatus(), 168, 45);
  tft.drawString("OpenSky mode: " + data.getOpenSkyModeLabel(), 8, 62);
  tft.drawString("HTTP: " + String(data.getLastHttpCode()) + "  Status: " + data.getLastApiStatus(), 8, 79);
  tft.drawString("Range: " + String(MAX_RANGE_NM, 0) + " nm   Tracked: " + String(data.getPlaneCount()), 8, 96);
  tft.drawString("Battery: " + String((int)data.getBatteryPercent()) + "%", 8, 113);
  tft.drawString("SD: " + sdStatus + "  Events: " + String(data.getEventCount()), 8, 181);
  tft.drawString("FPS: " + String(fps) + "  RAM: " + String((int)(ESP.getFreeHeap() / 1024)) + "k", 168, 113);
  tft.drawString("Trails: " + String(ENABLE_MOTION_TRAILS ? "enabled" : "off") + "  points " + String(TRAIL_POINTS), 8, 130);
  tft.drawString("Radar: incremental", 168, 130);
  tft.drawString(String("CPA + Alerts: enabled   ") + BUILD_LABEL, 8, 195);
  tft.drawString("Home lat: " + String(data.getHomeLat(), 6), 8, 147);
  tft.drawString("Home lon: " + String(data.getHomeLon(), 6), 8, 164);
  tft.drawString("Coords: " + data.getHomeLocationSource(), 168, 147);

  unsigned long age = data.getLastPlaneUpdateMs() ? (millis() - data.getLastPlaneUpdateMs()) / 1000 : 0;
  tft.drawString("Data age: " + String(age) + " sec", 168, 164);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  // Footer intentionally reserved for touch menu / future settings hint

  if (!menuDrawnForScreen) {
    drawTouchMenu(SCREEN_SYSTEM);
    menuDrawnForScreen = true;
  }
}

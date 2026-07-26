#include "InputCYD.h"
#include "CydSpi.h"

void InputCYD::begin() {
  lastAutoCycle = millis();
  refreshRequested = false;

#if ENABLE_TOUCH_NAV
  // Touch and SD share the one global VSPI object. CydSpi.h performs an
  // explicit pin handoff before either device accesses the peripheral.
  cydClaimTouchSpi(SPI);
  touch.begin(SPI);
  touch.setRotation(1);
#endif
}

void InputCYD::update() {
#if ENABLE_TOUCH_NAV
  int x = 0;
  int y = 0;
  if (readTouch(x, y)) {
    handleTouch(x, y);
    return;
  }
#endif

  if (!AUTO_CYCLE_SCREENS) return;

  if (millis() - lastAutoCycle >= AUTO_CYCLE_INTERVAL_MS) {
    lastAutoCycle = millis();
    if (screen == SCREEN_RADAR) screen = SCREEN_LIST;
    else if (screen == SCREEN_LIST) screen = SCREEN_TRAFFIC;
    else if (screen == SCREEN_TRAFFIC) screen = SCREEN_SYSTEM;
    else screen = SCREEN_RADAR;
  }
}

ScreenMode InputCYD::currentScreen() const {
  return screen;
}

void InputCYD::setScreen(ScreenMode newScreen) {
  screen = newScreen;
}

bool InputCYD::listHistoryActive() const {
  return historyActive;
}

bool InputCYD::consumeRefreshRequest() {
  if (!refreshRequested) return false;
  refreshRequested = false;
  return true;
}

#if ENABLE_TOUCH_NAV
bool InputCYD::hitRefreshButton(int x, int y) {
  return x >= REFRESH_BTN_X && x < (REFRESH_BTN_X + REFRESH_BTN_W) &&
         y >= REFRESH_BTN_Y && y < (REFRESH_BTN_Y + REFRESH_BTN_H);
}

bool InputCYD::readTouch(int &x, int &y) {
  // Reclaim VSPI for touch pins if SD just used the bus.
  cydClaimTouchSpi(SPI);

  if (!touch.touched()) return false;
  if (millis() - lastTouchMs < 250) return false; // debounce

  TS_Point p = touch.getPoint();

  // CYD landscape mapping starting point. If buttons feel mirrored, swap/invert these maps.
  x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
  y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);
  x = constrain(x, 0, SCREEN_WIDTH - 1);
  y = constrain(y, 0, SCREEN_HEIGHT - 1);

  lastTouchMs = millis();
  return true;
}

void InputCYD::handleTouch(int x, int y) {
  // Manual traffic refresh — only on radar screen, dedicated hit box.
  if (screen == SCREEN_RADAR && hitRefreshButton(x, y)) {
    refreshRequested = true;
    Serial.println(F("[Touch] RFRSH button"));
    return;
  }

  if (screen == SCREEN_LIST &&
      y >= LIST_TAB_Y && y < LIST_TAB_Y + LIST_TAB_H) {
    historyActive = x >= SCREEN_WIDTH / 2;
    Serial.println(historyActive
        ? F("[Touch] LIST tab: HISTORY")
        : F("[Touch] LIST tab: LIVE AIRCRAFT"));
    return;
  }

  // Bottom nav bar only for screen changes.
  if (y < TOUCH_BAR_Y) return;

  int w = SCREEN_WIDTH / 4;
  if (x < w) screen = SCREEN_RADAR;
  else if (x < w * 2) screen = SCREEN_LIST;
  else if (x < w * 3) screen = SCREEN_TRAFFIC;
  else screen = SCREEN_SYSTEM;
}
#endif

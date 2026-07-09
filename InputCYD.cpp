#include "InputCYD.h"

void InputCYD::begin() {
  lastAutoCycle = millis();

#if ENABLE_TOUCH_NAV
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
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

#if ENABLE_TOUCH_NAV
bool InputCYD::readTouch(int &x, int &y) {
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
  if (y < TOUCH_BAR_Y) return;

  int w = SCREEN_WIDTH / 4;
  if (x < w) screen = SCREEN_RADAR;
  else if (x < w * 2) screen = SCREEN_LIST;
  else if (x < w * 3) screen = SCREEN_TRAFFIC;
  else screen = SCREEN_SYSTEM;
}
#endif

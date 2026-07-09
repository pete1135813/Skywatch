#pragma once
#include <Arduino.h>
#include "DisplayCYD.h"
#include "config.h"

#if ENABLE_TOUCH_NAV
  #include <SPI.h>
  #include <XPT2046_Touchscreen.h>
#endif

class InputCYD {
public:
  void begin();
  void update();
  ScreenMode currentScreen() const;
  void setScreen(ScreenMode screen);

private:
  ScreenMode screen = SCREEN_RADAR;
  unsigned long lastAutoCycle = 0;
  unsigned long lastTouchMs = 0;

#if ENABLE_TOUCH_NAV
  SPIClass touchSPI = SPIClass(VSPI);
  XPT2046_Touchscreen touch = XPT2046_Touchscreen(TOUCH_CS, TOUCH_IRQ);
  bool readTouch(int &x, int &y);
  void handleTouch(int x, int y);
#endif
};

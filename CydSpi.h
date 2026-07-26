#pragma once
//
// CYD SPI ownership helper
// -----------------------
// ESP32 only has two hardware SPI peripherals. On the Cheap Yellow Display:
//   - TFT is usually driven by TFT_eSPI on HSPI (pins 12/13/14/15)
//   - microSD uses VSPI pins: SCK=18, MISO=19, MOSI=23, CS=5
//   - XPT2046 touch uses separate pins but was also bound to VSPI
//
// Touch and SD cannot both "own" VSPI at once. Whoever runs last remaps the
// pins. Both devices MUST use the same SPIClass object: two SPIClass instances
// can each believe VSPI is already initialized while the hardware is actually
// mapped to the other device.
//
// Rule: call cydClaimSdSpi() before any SD access, and cydClaimTouchSpi()
// before any touch access. Each handoff ends and restarts the shared bus so
// the ESP32 pin matrix is physically remapped, not merely updated in software.

#include <Arduino.h>
#include <SPI.h>
#include "config.h"

enum class CydSpiOwner : uint8_t { None = 0, Sd, Touch };

inline CydSpiOwner cydSpiOwner = CydSpiOwner::None;

inline void cydDeselectTouch() {
#if ENABLE_TOUCH_NAV
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
#endif
}

inline void cydDeselectSd() {
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
}

// Reclaim default SPI (VSPI) for the microSD slot.
inline void cydClaimSdSpi() {
  if (cydSpiOwner == CydSpiOwner::Sd) return;
  cydDeselectTouch();
  cydDeselectSd();
  // Keep the TFT off the bus while SD is active (separate pins, shared start-up noise).
  pinMode(TFT_CS_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH);
  SPI.end();
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  cydSpiOwner = CydSpiOwner::Sd;
}

// Reclaim VSPI for the touch controller pins.
inline void cydClaimTouchSpi(SPIClass &touchSPI) {
  if (cydSpiOwner == CydSpiOwner::Touch) return;
  cydDeselectSd();
  cydDeselectTouch();
  touchSPI.end();
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  cydSpiOwner = CydSpiOwner::Touch;
}

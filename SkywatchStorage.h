#pragma once
#include <Arduino.h>
#include <SD.h>
#include "config.h"

class SkywatchStorage {
public:
  void begin();
  bool isReady() const;
  String statusText() const;
  bool ensureFolders();
  void logEvent(const String &line);
  bool fileExists(const char *path) const;
private:
  bool ready = false;
  String status = "Not initialized";
};

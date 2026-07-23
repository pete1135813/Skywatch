#pragma once
#include <Arduino.h>
#include <SD.h>
#include "config.h"

struct Plane;
class SkywatchData;

class SkywatchStorage {
public:
  void begin();
  bool isReady() const;
  String statusText() const;
  bool ensureFolders();
  bool loadRuntimeConfig(SkywatchData &data);
  void logEvent(const String &line);
  void recordAircraft(const Plane &p);
  void writeDailyStats(const SkywatchData &data);
  bool fileExists(const char *path) const;

private:
  bool ready = false;
  String status = "Not initialized";

  void claimBus() const;
  String safeId(const String &raw) const;
  String timestamp() const;
  bool timeIsValid() const;
  time_t nowEpoch() const;

  // One-pass profile load (avoids 5 open/scan/close cycles per plane).
  struct ProfileState {
    bool exists = false;
    int seen = 0;
    float avgAlt = 0;
    float avgSpeed = 0;
    float closest = 9999.0f;
    time_t lastWriteEpoch = 0;
    unsigned long lastWriteBootMs = 0;
    char firstSeen[24] = {0};
  };

  bool loadProfile(const char *path, ProfileState &out) const;
};

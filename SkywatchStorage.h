#pragma once
#include <Arduino.h>
#include <SD.h>
#include "config.h"

struct Plane;
struct SkyEvent;
class SkywatchData;

struct StoredEvent {
  char timestamp[24] = {0};
  char type[16] = {0};
  char callsign[16] = {0};
  float distanceNm = 0;
  float cpaNm = 0;
  int etaSec = -1;
  uint8_t threatLevel = 0;
  bool valid = false;
};

class SkywatchStorage {
public:
  void begin();
  bool isReady() const;
  String statusText() const;
  bool ensureFolders();
  bool loadRuntimeConfig(SkywatchData &data);
  bool logEvent(const SkyEvent &event);
  int loadRecentEvents();
  int getHistoryCount() const;
  const StoredEvent& getHistoryEvent(int index) const;
  String getHistoryStatus() const;
  void recordAircraft(const Plane &p);
  void writeDailyStats(const SkywatchData &data);
  bool fileExists(const char *path) const;

private:
  bool ready = false;
  String status = "Not initialized";
  StoredEvent history[EVENT_HISTORY_MAX_ENTRIES];
  int historyCount = 0;
  String historyStatus = "Not loaded";
  uint8_t eventLogFailureCount = 0;
  unsigned long eventLogRetryAfterMs = 0;

  void claimBus() const;
  bool ensureEventLogPath();
  void scheduleEventLogRetry(const char *reason);
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
  bool parseEventLine(char *line, StoredEvent &out) const;
  void rotateEventLogIfNeeded();
};

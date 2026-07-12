#include "SkywatchStorage.h"
#include "SkywatchData.h"
#include "CydSpi.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void SkywatchStorage::claimBus() const {
  cydClaimSdSpi();
}

bool SkywatchStorage::timeIsValid() const {
  return nowEpoch() > 1700000000;
}

time_t SkywatchStorage::nowEpoch() const {
  return time(nullptr);
}

String SkywatchStorage::safeId(const String &raw) const {
  String id = raw;
  id.trim();
  if (id.length() == 0) id = "UNKNOWN";
  id.toUpperCase();
  char out[17];
  size_t n = 0;
  for (unsigned i = 0; i < id.length() && n < 16; i++) {
    char c = id[i];
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out[n++] = c;
    }
  }
  if (n == 0) {
    strcpy(out, "UNKNOWN");
    n = 7;
  }
  out[n] = '\0';
  return String(out);
}

String SkywatchStorage::timestamp() const {
  time_t now = nowEpoch();
  if (now > 1700000000) {
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    return String(buf);
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "boot+%lus", (unsigned long)(millis() / 1000UL));
  return String(buf);
}

bool SkywatchStorage::loadProfile(const char *path, ProfileState &out) const {
  out = ProfileState();
  if (!ready || !SD.exists(path)) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  out.exists = true;

  char line[96];
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    // trim CR/spaces
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
      line[--n] = '\0';
    }
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;

    auto afterColon = [&](const char *key) -> const char * {
      size_t klen = strlen(key);
      if (strncmp(p, key, klen) != 0) return nullptr;
      const char *v = p + klen;
      if (*v != ':') return nullptr;
      v++;
      while (*v == ' ' || *v == '\t') v++;
      return v;
    };

    if (const char *v = afterColon("Seen")) {
      out.seen = atoi(v);
    } else if (const char *v = afterColon("First Seen")) {
      strncpy(out.firstSeen, v, sizeof(out.firstSeen) - 1);
      out.firstSeen[sizeof(out.firstSeen) - 1] = '\0';
    } else if (const char *v = afterColon("Average Altitude Ft")) {
      out.avgAlt = atof(v);
    } else if (const char *v = afterColon("Average Speed Kt")) {
      out.avgSpeed = atof(v);
    } else if (const char *v = afterColon("Closest Pass Nm")) {
      out.closest = atof(v);
    } else if (const char *v = afterColon("LastWriteEpoch")) {
      out.lastWriteEpoch = (time_t)strtoul(v, nullptr, 10);
    } else if (const char *v = afterColon("LastWriteBootMs")) {
      out.lastWriteBootMs = strtoul(v, nullptr, 10);
    }
  }
  f.close();
  return true;
}

void SkywatchStorage::recordAircraft(const Plane &p) {
  if (!ready || !p.valid) return;
  claimBus();

  String id = p.callsign.length() ? p.callsign : p.icao;
  id = safeId(id);

  char path[40];
  snprintf(path, sizeof(path), "/planes/%s.txt", id.c_str());

  ProfileState prev;
  loadProfile(path, prev);

  int seen = prev.seen;
  if (!prev.exists || seen <= 0) {
    seen = 1;
  } else {
    bool newSighting = false;
    if (timeIsValid() && prev.lastWriteEpoch > 1700000000) {
      time_t gap = nowEpoch() - prev.lastWriteEpoch;
      if (gap >= (time_t)AIRCRAFT_SEEN_GAP_SEC) newSighting = true;
    } else if (prev.lastWriteBootMs > 0) {
      if (millis() - prev.lastWriteBootMs >= AIRCRAFT_SEEN_GAP_SEC * 1000UL) newSighting = true;
    } else {
      // Legacy profiles without write stamps: treat first post-upgrade write as same sighting.
      newSighting = false;
    }
    if (newSighting) seen++;
  }

  char firstSeen[24];
  if (prev.firstSeen[0]) {
    strncpy(firstSeen, prev.firstSeen, sizeof(firstSeen) - 1);
    firstSeen[sizeof(firstSeen) - 1] = '\0';
  } else {
    String ts = timestamp();
    strncpy(firstSeen, ts.c_str(), sizeof(firstSeen) - 1);
    firstSeen[sizeof(firstSeen) - 1] = '\0';
  }

  float oldClosest = prev.exists ? prev.closest : 9999.0f;
  if (oldClosest <= 0.0f) oldClosest = 9999.0f;

  float avgAlt;
  float avgSpeed;
  if (!prev.exists) {
    avgAlt = p.altitudeFt;
    avgSpeed = p.speedKt;
  } else if (seen > prev.seen) {
    // New sighting: fold into long-term average by sighting count.
    avgAlt = ((prev.avgAlt * (float)(seen - 1)) + p.altitudeFt) / (float)seen;
    avgSpeed = ((prev.avgSpeed * (float)(seen - 1)) + p.speedKt) / (float)seen;
  } else {
    // Same sighting: light EWMA so continuous flushes don't skew averages.
    avgAlt = prev.avgAlt * 0.8f + p.altitudeFt * 0.2f;
    avgSpeed = prev.avgSpeed * 0.8f + p.speedKt * 0.2f;
  }

  float currentClosest = p.cpaDistanceNm > 0.01f ? p.cpaDistanceNm : p.distanceNm;
  float closest = fminf(oldClosest, currentClosest);

  String ts = timestamp();
  time_t epoch = timeIsValid() ? nowEpoch() : 0;

  File f = SD.open(path, FILE_WRITE);
  if (!f) return;

  f.println(F("Skywatch Aircraft Profile"));
  f.printf("ID: %s\n", id.c_str());
  f.printf("ICAO: %s\n", p.icao.c_str());
  f.printf("Callsign: %s\n", p.callsign.c_str());
  f.printf("Seen: %d\n", seen);
  f.printf("First Seen: %s\n", firstSeen);
  f.printf("Last Seen: %s\n", ts.c_str());
  f.printf("Last Distance Nm: %.1f\n", p.distanceNm);
  f.printf("Last Bearing Deg: %.0f\n", p.bearingDeg);
  f.printf("Last Heading Deg: %.0f\n", p.headingDeg);
  f.printf("Last Altitude Ft: %.0f\n", p.altitudeFt);
  f.printf("Last Speed Kt: %.0f\n", p.speedKt);
  f.printf("Average Altitude Ft: %.0f\n", avgAlt);
  f.printf("Average Speed Kt: %.0f\n", avgSpeed);
  f.printf("Closest Pass Nm: %.1f\n", closest);
  f.printf("Last CPA Nm: %.1f\n", p.cpaDistanceNm);
  f.printf("Last ETA Sec: %d\n", p.cpaEtaSec);
  f.printf("Last Threat: %u\n", (unsigned)p.threatLevel);
  f.printf("LastWriteEpoch: %lu\n", (unsigned long)epoch);
  f.printf("LastWriteBootMs: %lu\n", (unsigned long)millis());
  f.close();
}

void SkywatchStorage::writeDailyStats(const SkywatchData &data) {
  if (!ready) return;
  claimBus();

  File f = SD.open("/stats/today.txt", FILE_WRITE);
  if (!f) return;
  String ts = timestamp();
  f.println(F("Skywatch Daily Snapshot"));
  f.printf("Updated: %s\n", ts.c_str());
  f.printf("Tracked Now: %d\n", data.getPlaneCount());
  f.printf("Active Source: %s\n", data.getActiveSourceLabel().c_str());
  f.printf("HTTP: %d\n", data.getLastHttpCode());
  f.printf("Status: %s\n", data.getLastApiStatus().c_str());
  f.printf("Events In RAM: %d\n", data.getEventCount());
  f.printf("OpenSky Mode: %s\n", data.getOpenSkyModeLabel().c_str());
  f.printf("Range Nm: %.0f\n", MAX_RANGE_NM);
  f.printf("Free Heap: %u\n", (unsigned)ESP.getFreeHeap());
  f.close();
}

void SkywatchStorage::begin() {
#if ENABLE_SD_STORAGE
  Serial.println(F("[SD] Initializing..."));
  claimBus();
  // Explicit SPI + clock. Try full speed first, then a slower fallback for finicky cards.
  bool mounted = SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ);
  if (!mounted) {
    Serial.println(F("[SD] begin@10MHz failed — retry @4MHz"));
    claimBus();
    mounted = SD.begin(SD_CS_PIN, SPI, 4000000);
  }
  if (mounted) {
    ready = true;
    status = "OK";
    ensureFolders();
    Serial.println(F("[SD] OK"));
    uint8_t cardType = SD.cardType();
    Serial.printf("[SD] cardType=%u size=%lluMB\n",
                  (unsigned)cardType,
                  (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
    if (SD.exists(SD_LOGO_PATH)) {
      Serial.printf("[SD] Logo found: %s\n", SD_LOGO_PATH);
    } else if (SD.exists(SD_LOGO_PATH_ALT)) {
      Serial.printf("[SD] Logo found: %s\n", SD_LOGO_PATH_ALT);
    } else {
      Serial.println(F("[SD] Logo not found at /logos/skywatch_logo.bmp or /skywatch_logo.bmp"));
    }
  } else {
    ready = false;
    status = "NOT FOUND";
    Serial.println(F("[SD] NOT FOUND"));
  }
#else
  ready = false;
  status = "disabled";
#endif
}

bool SkywatchStorage::isReady() const { return ready; }
String SkywatchStorage::statusText() const { return status; }

bool SkywatchStorage::ensureFolders() {
  if (!ready) return false;
  claimBus();
  const char *dirs[] = {"/events", "/tracks", "/planes", "/stats", "/config", "/logos"};
  for (uint8_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
    if (!SD.exists(dirs[i])) SD.mkdir(dirs[i]);
  }
  return true;
}

void SkywatchStorage::logEvent(const String &line) {
  if (!ready) return;
  claimBus();
  File f = SD.open(SD_EVENT_LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.println(line);
  f.close();
}

bool SkywatchStorage::fileExists(const char *path) const {
  if (!ready) return false;
  claimBus();
  return SD.exists(path);
}

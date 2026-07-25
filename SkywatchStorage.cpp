#include "SkywatchStorage.h"
#include "SkywatchData.h"
#include "CydSpi.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static StoredEvent EMPTY_STORED_EVENT;

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
  bool allReady = true;
  for (uint8_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
    if (!SD.exists(dirs[i]) && !SD.mkdir(dirs[i])) {
      allReady = false;
      Serial.printf("[SD] Folder create failed: %s\n", dirs[i]);
    }
  }
  return allReady;
}

bool SkywatchStorage::loadRuntimeConfig(SkywatchData &data) {
  if (!ready) {
    Serial.println(F("[Config] SD unavailable; using compiled defaults"));
    return false;
  }

  claimBus();
  File f = SD.open(SD_SETTINGS_PATH, FILE_READ);
  if (!f) {
    Serial.printf("[Config] %s not found; using compiled defaults\n", SD_SETTINGS_PATH);
    return false;
  }

  bool applied = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) continue;

    int separator = line.indexOf('=');
    if (separator <= 0) continue;

    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key.trim();
    value.trim();

    if (key.equalsIgnoreCase("LocalADSBURL") && value.length()) {
      data.setLocalAdsbUrl(value);
      applied = true;
    }
  }
  f.close();

  Serial.println(applied
      ? F("[Config] Runtime settings applied")
      : F("[Config] No runtime overrides found; using compiled defaults"));
  return applied;
}

void SkywatchStorage::rotateEventLogIfNeeded() {
  if (!ready || !SD.exists(SD_EVENT_LOG_PATH)) return;
  File current = SD.open(SD_EVENT_LOG_PATH, FILE_READ);
  if (!current) return;
  size_t bytes = current.size();
  current.close();
  if (bytes < EVENT_LOG_MAX_BYTES) return;

  if (SD.exists(SD_EVENT_ARCHIVE_PATH)) SD.remove(SD_EVENT_ARCHIVE_PATH);
  if (SD.rename(SD_EVENT_LOG_PATH, SD_EVENT_ARCHIVE_PATH)) {
    Serial.printf("[SD] Event log rotated at %u bytes\n", (unsigned)bytes);
  }
}

bool SkywatchStorage::ensureEventLogPath() {
  File eventsDir = SD.open("/events", FILE_READ);
  if (eventsDir) {
    bool isDirectory = eventsDir.isDirectory();
    eventsDir.close();
    if (!isDirectory) {
      scheduleEventLogRetry("/events exists but is not a folder");
      return false;
    }
    return true;
  }

  if (!SD.mkdir("/events")) {
    scheduleEventLogRetry("could not create /events folder");
    return false;
  }

  eventsDir = SD.open("/events", FILE_READ);
  bool created = eventsDir && eventsDir.isDirectory();
  if (eventsDir) eventsDir.close();
  if (!created) {
    scheduleEventLogRetry("/events folder verification failed");
    return false;
  }

  Serial.println(F("[SD] Recreated missing /events folder"));
  return true;
}

void SkywatchStorage::scheduleEventLogRetry(const char *reason) {
  if (eventLogFailureCount < 6) eventLogFailureCount++;

  // 5s, 10s, 20s, then 30s. One failing SD path must never monopolize loop().
  uint8_t shift = min((int)eventLogFailureCount - 1, 3);
  unsigned long retryDelayMs = 5000UL << shift;
  if (retryDelayMs > 30000UL) retryDelayMs = 30000UL;
  eventLogRetryAfterMs = millis() + retryDelayMs;

  historyStatus = String(reason) + " - retry " + String(retryDelayMs / 1000UL) + "s";
  Serial.printf("[SD] Event logger paused: %s; retry in %lus\n",
                reason, retryDelayMs / 1000UL);
}

bool SkywatchStorage::logEvent(const SkyEvent &event) {
  if (!ready) {
    historyStatus = "SD unavailable";
    Serial.println(F("[SD] Event log skipped: storage not ready"));
    return false;
  }

  // Signed subtraction keeps this comparison safe across millis() rollover.
  if (eventLogRetryAfterMs != 0 &&
      (int32_t)(millis() - eventLogRetryAfterMs) < 0) {
    return false;
  }

  claimBus();
  if (!ensureEventLogPath()) return false;
  rotateEventLogIfNeeded();

  bool fileExisted = SD.exists(SD_EVENT_LOG_PATH);
  File f = SD.open(SD_EVENT_LOG_PATH, FILE_APPEND);
  if (!f && !fileExisted) {
    // Some SD/FAT combinations do not create a new file in append mode.
    // Create it once with write mode, then reopen without risking truncation.
    File created = SD.open(SD_EVENT_LOG_PATH, FILE_WRITE);
    if (created) {
      created.close();
      f = SD.open(SD_EVENT_LOG_PATH, FILE_APPEND);
      if (f) Serial.println(F("[SD] Created missing event log file"));
    }
  }
  if (!f) {
    scheduleEventLogRetry("event log open failed");
    return false;
  }

  String type = event.type;
  String callsign = event.callsign;
  type.replace("|", "/");
  callsign.replace("|", "/");
  type.trim();
  callsign.trim();
  if (type.length() == 0) type = "EVENT";
  if (callsign.length() == 0) callsign = "UNKNOWN";

  String ts = timestamp();
  size_t written = f.printf("%s|%s|%s|%.1f|%.1f|%d|%u\n",
                            ts.c_str(), type.c_str(), callsign.c_str(),
                            event.distanceNm, event.cpaNm, event.etaSec,
                            (unsigned)event.threatLevel);
  f.flush();
  size_t fileSize = f.size();
  f.close();

  if (written == 0) {
    scheduleEventLogRetry("event log write failed");
    return false;
  }

  if (eventLogFailureCount > 0) {
    Serial.println(F("[SD] Event logger recovered"));
  }
  eventLogFailureCount = 0;
  eventLogRetryAfterMs = 0;
  historyStatus = "New event saved";
  Serial.printf("[SD] Event logged: %s %s (%u bytes, file %u bytes)\n",
                type.c_str(), callsign.c_str(), (unsigned)written,
                (unsigned)fileSize);
  return true;
}

bool SkywatchStorage::parseEventLine(char *line, StoredEvent &out) const {
  out = StoredEvent();
  if (!line || !line[0]) return false;

  // Build 28 pipe-delimited format:
  // timestamp|type|callsign|distance|cpa|eta|threat
  if (strchr(line, '|')) {
    char *save = nullptr;
    char *fields[7] = {nullptr};
    int count = 0;
    for (char *token = strtok_r(line, "|", &save);
         token && count < 7;
         token = strtok_r(nullptr, "|", &save)) {
      fields[count++] = token;
    }
    if (count != 7) return false;

    strncpy(out.timestamp, fields[0], sizeof(out.timestamp) - 1);
    strncpy(out.type, fields[1], sizeof(out.type) - 1);
    strncpy(out.callsign, fields[2], sizeof(out.callsign) - 1);
    out.distanceNm = atof(fields[3]);
    out.cpaNm = atof(fields[4]);
    out.etaSec = atoi(fields[5]);
    out.threatLevel = (uint8_t)constrain(atoi(fields[6]), 0, 3);
    out.valid = true;
    return true;
  }

  // Backward-compatible Build 26/27 format:
  // 317s NEW_TARGET VOI731 dist=12.5 cpa=6.4 eta=251 threat=1
  unsigned threat = 0;
  int matched = sscanf(line, "%23s %15s %15s dist=%f cpa=%f eta=%d threat=%u",
                       out.timestamp, out.type, out.callsign,
                       &out.distanceNm, &out.cpaNm, &out.etaSec, &threat);
  if (matched != 7) return false;
  out.threatLevel = (uint8_t)constrain((int)threat, 0, 3);
  out.valid = true;
  return true;
}

int SkywatchStorage::loadRecentEvents() {
  historyCount = 0;
  for (int i = 0; i < EVENT_HISTORY_MAX_ENTRIES; i++) history[i] = StoredEvent();
  if (!ready) {
    historyStatus = "SD unavailable";
    Serial.println(F("[SD] History load skipped: storage not ready"));
    return 0;
  }

  claimBus();
  File f = SD.open(SD_EVENT_LOG_PATH, FILE_READ);
  if (!f) {
    historyStatus = "No event log file";
    Serial.printf("[SD] History file not found/openable: %s\n", SD_EVENT_LOG_PATH);
    return 0;
  }

  const size_t fileSize = f.size();
  if (fileSize == 0) {
    f.close();
    historyStatus = "Event log is empty";
    Serial.printf("[SD] History file is empty: %s\n", SD_EVENT_LOG_PATH);
    return 0;
  }
  const size_t start = fileSize > EVENT_HISTORY_TAIL_BYTES
      ? fileSize - EVENT_HISTORY_TAIL_BYTES
      : 0;
  if (!f.seek(start)) {
    f.close();
    historyStatus = "Event log seek failed";
    Serial.printf("[SD] History seek failed: %s offset %u\n",
                  SD_EVENT_LOG_PATH, (unsigned)start);
    return 0;
  }

  // Fixed static buffer: no heap allocation and no full-file read.
  static char tail[EVENT_HISTORY_TAIL_BYTES + 1];
  size_t bytes = f.readBytes(tail, EVENT_HISTORY_TAIL_BYTES);
  f.close();
  tail[bytes] = '\0';

  char *cursor = tail;
  if (start > 0) {
    // The tail may begin in the middle of a line; discard that partial record.
    char *firstNewline = strchr(cursor, '\n');
    if (!firstNewline) {
      historyStatus = "No complete history rows";
      Serial.printf("[SD] History tail has no complete line (%u bytes)\n",
                    (unsigned)bytes);
      return 0;
    }
    cursor = firstNewline + 1;
  }

  StoredEvent chronological[EVENT_HISTORY_MAX_ENTRIES];
  int chronologicalCount = 0;
  int lineCount = 0;
  int rejectedCount = 0;
  char *lineSave = nullptr;
  for (char *line = strtok_r(cursor, "\n", &lineSave);
       line;
       line = strtok_r(nullptr, "\n", &lineSave)) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) {
      line[--len] = '\0';
    }

    lineCount++;
    StoredEvent parsed;
    if (!parseEventLine(line, parsed)) {
      rejectedCount++;
      continue;
    }

    if (chronologicalCount < EVENT_HISTORY_MAX_ENTRIES) {
      chronological[chronologicalCount++] = parsed;
    } else {
      for (int i = 1; i < EVENT_HISTORY_MAX_ENTRIES; i++) {
        chronological[i - 1] = chronological[i];
      }
      chronological[EVENT_HISTORY_MAX_ENTRIES - 1] = parsed;
    }
  }

  historyCount = chronologicalCount;
  for (int i = 0; i < chronologicalCount; i++) {
    history[i] = chronological[chronologicalCount - 1 - i];
  }

  if (historyCount > 0) {
    historyStatus = String(historyCount) + " event" +
                    (historyCount == 1 ? "" : "s") + " loaded";
  } else if (lineCount == 0) {
    historyStatus = "Event log has no rows";
  } else {
    historyStatus = String("0 parsed / ") + lineCount + " rows";
  }

  Serial.printf("[SD] History: file=%u tail=%u lines=%d loaded=%d rejected=%d\n",
                (unsigned)fileSize, (unsigned)bytes, lineCount,
                historyCount, rejectedCount);
  return historyCount;
}

int SkywatchStorage::getHistoryCount() const {
  return historyCount;
}

String SkywatchStorage::getHistoryStatus() const {
  return historyStatus;
}

const StoredEvent& SkywatchStorage::getHistoryEvent(int index) const {
  if (index < 0 || index >= historyCount) return EMPTY_STORED_EVENT;
  return history[index];
}

bool SkywatchStorage::fileExists(const char *path) const {
  if (!ready) return false;
  claimBus();
  return SD.exists(path);
}

#include "SkywatchStorage.h"

void SkywatchStorage::begin() {
#if ENABLE_SD_STORAGE
  Serial.println("[SD] Initializing...");
  if (SD.begin(SD_CS_PIN)) {
    ready = true;
    status = "OK";
    ensureFolders();
    Serial.println("[SD] OK");
  } else {
    ready = false;
    status = "NOT FOUND";
    Serial.println("[SD] NOT FOUND");
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
  const char *dirs[] = {"/events", "/tracks", "/planes", "/stats", "/config", "/logos"};
  for (uint8_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
    if (!SD.exists(dirs[i])) SD.mkdir(dirs[i]);
  }
  return true;
}

void SkywatchStorage::logEvent(const String &line) {
  if (!ready) return;
  File f = SD.open(SD_EVENT_LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.println(line);
  f.close();
}

bool SkywatchStorage::fileExists(const char *path) const {
  if (!ready) return false;
  return SD.exists(path);
}

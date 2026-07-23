#include "SkywatchData.h"
#include <math.h>
#include <time.h>

static Plane EMPTY_PLANE;
static SkyEvent EMPTY_EVENT;

void SkywatchData::begin() {
  clearPlanes();
  updateBattery();
}

void SkywatchData::clearPlanes() {
  planeCount = 0;
  for (int i = 0; i < MAX_PLANES; i++) planes[i] = Plane();
}

void SkywatchData::setHomeCoords(float lat, float lon, bool overridden) {
  homeLat = lat;
  homeLon = lon;
  coordsOverridden = overridden;
}

float SkywatchData::getHomeLat() const { return homeLat; }
float SkywatchData::getHomeLon() const { return homeLon; }
bool SkywatchData::coordsWereOverridden() const { return coordsOverridden; }
int SkywatchData::getPlaneCount() const { return planeCount; }
float SkywatchData::getBatteryPercent() const { return batteryPercent; }
unsigned long SkywatchData::getLastPlaneUpdateMs() const { return lastPlaneUpdateMs; }
String SkywatchData::getLastApiStatus() const { return lastApiStatus; }
int SkywatchData::getLastHttpCode() const { return lastHttpCode; }
String SkywatchData::getActiveSourceLabel() const { return activeSource; }
String SkywatchData::getLocalStatus() const { return localStatus; }
String SkywatchData::getLocalAdsbUrl() const { return localAdsbUrl; }

void SkywatchData::setLocalAdsbUrl(const String &url) {
  String candidate = url;
  candidate.trim();
  if (!candidate.startsWith("http://")) {
    Serial.println(F("[Local ADS-B] Ignoring invalid URL (plain HTTP LAN URL required)"));
    return;
  }
  localAdsbUrl = candidate;
  lastLocalAttemptMs = 0;
  localAdsbHealthy = false;
  Serial.print(F("[Local ADS-B] Runtime URL: "));
  Serial.println(localAdsbUrl);
}

int SkywatchData::getEventCount() const { return eventCount; }
const SkyEvent& SkywatchData::getEvent(int index) const {
  if (index < 0 || index >= eventCount) return EMPTY_EVENT;
  return events[index];
}
const SkyEvent& SkywatchData::getLatestEvent() const {
  if (eventCount <= 0) return EMPTY_EVENT;
  return events[0];
}
bool SkywatchData::hasRecentAlert(unsigned long maxAgeMs) const {
  return eventCount > 0 && (millis() - events[0].createdMs) <= maxAgeMs;
}
String SkywatchData::eventLogLine(int index) const {
  if (index < 0 || index >= eventCount) return String();
  const SkyEvent &e = events[index];
  String call = e.callsign.length() ? e.callsign : "UNKNOWN";
  String line = String(millis() / 1000) + "s " + e.type + " " + call +
                " dist=" + String(e.distanceNm, 1) + " cpa=" + String(e.cpaNm, 1) +
                " eta=" + String(e.etaSec) + " threat=" + String(e.threatLevel);
  return line;
}
void SkywatchData::markEventLogged(int index) {
  if (index >= 0 && index < eventCount) events[index].logged = true;
}

String SkywatchData::getOpenSkyModeLabel() const {
#if OPENSKY_CALL_MODE == OPENSKY_MODE_MANUAL_ONLY
  return "MANUAL";
#elif OPENSKY_CALL_MODE == OPENSKY_MODE_60_SEC
  return "60s";
#else
  return "360s";
#endif
}

const Plane& SkywatchData::getPlane(int index) const {
  if (index < 0 || index >= planeCount) return EMPTY_PLANE;
  return planes[index];
}

void SkywatchData::saveOldPlanes(Plane oldPlanes[], int &oldCount) const {
  oldCount = planeCount;
  for (int i = 0; i < MAX_PLANES; i++) oldPlanes[i] = planes[i];
}

int SkywatchData::findOldPlaneIndex(const Plane oldPlanes[], int oldCount, const String &icao, const String &callsign) const {
  for (int i = 0; i < oldCount; i++) {
    if (!oldPlanes[i].valid) continue;
    if (icao.length() && oldPlanes[i].icao == icao) return i;
    if (callsign.length() && oldPlanes[i].callsign == callsign) return i;
  }
  return -1;
}

void SkywatchData::inheritTrailFromOld(Plane &dst, const Plane &oldPlane) {
  if (!oldPlane.valid) return;
  for (int i = TRAIL_POINTS - 1; i > 0; i--) dst.trail[i] = oldPlane.trail[i - 1];
  dst.trail[0].distanceNm = oldPlane.distanceNm;
  dst.trail[0].bearingDeg = oldPlane.bearingDeg;
  dst.trail[0].valid = true;
}

void SkywatchData::addEvent(const String &type, const Plane &p) {
  // Push newest event to the front. Keep last 20 in RAM.
  for (int i = 19; i > 0; i--) events[i] = events[i - 1];
  SkyEvent &e = events[0];
  e.type = type;
  e.callsign = p.callsign.length() ? p.callsign : p.icao;
  e.distanceNm = p.distanceNm;
  e.cpaNm = p.cpaDistanceNm;
  e.etaSec = p.cpaEtaSec;
  e.threatLevel = p.threatLevel;
  e.createdMs = millis();
  e.logged = false;
  if (eventCount < 20) eventCount++;
  Serial.println("[Event] " + eventLogLine(0));
}

void SkywatchData::finishPlaneSet(Plane oldPlanes[], int oldCount) {
  for (int i = 0; i < planeCount; i++) {
    int oldIndex = findOldPlaneIndex(oldPlanes, oldCount, planes[i].icao, planes[i].callsign);
    bool wasKnown = oldIndex >= 0;
    bool wasApproaching = wasKnown ? oldPlanes[oldIndex].approaching : false;
    uint8_t oldThreat = wasKnown ? oldPlanes[oldIndex].threatLevel : 0;

    if (wasKnown) inheritTrailFromOld(planes[i], oldPlanes[oldIndex]);
    computeAwareness(planes[i]);
    planes[i].valid = true;

#if ENABLE_OBSERVER_ALERTS
    if (!wasKnown) {
      addEvent("NEW_TARGET", planes[i]);
    } else if (!wasApproaching && planes[i].approaching) {
      addEvent("APPROACHING", planes[i]);
    } else if (planes[i].threatLevel > oldThreat && planes[i].threatLevel > 0) {
      addEvent("THREAT_UP", planes[i]);
    }
#endif
  }
  lastPlaneUpdateMs = millis();
}

void SkywatchData::computeAwareness(Plane &p) {
  // Closest Point of Approach to the observer.
  // Coordinates: x=east, y=north in nautical miles. Heading: 0=N, 90=E.
  float br = p.bearingDeg * PI / 180.0f;
  float hd = p.headingDeg * PI / 180.0f;

  float rx = p.distanceNm * sin(br);
  float ry = p.distanceNm * cos(br);

  // Aircraft ground velocity in nm/hour.
  float vx = p.speedKt * sin(hd);
  float vy = p.speedKt * cos(hd);
  float v2 = vx * vx + vy * vy;

  p.cpaDistanceNm = p.distanceNm;
  p.cpaEtaSec = -1;
  p.closingRateKt = 0;
  p.approaching = false;
  p.threatLevel = 0;

  if (v2 < 1.0f || p.distanceNm <= 0.01f) return;

  float dot = rx * vx + ry * vy;
  float range = sqrt(rx * rx + ry * ry);
  p.closingRateKt = -dot / range;
  p.approaching = p.closingRateKt > 5.0f;

  float tHours = -dot / v2;
  if (tHours < 0) {
    p.cpaEtaSec = -1;
    p.cpaDistanceNm = p.distanceNm;
  } else {
    float cpaX = rx + vx * tHours;
    float cpaY = ry + vy * tHours;
    p.cpaDistanceNm = sqrt(cpaX * cpaX + cpaY * cpaY);
    p.cpaEtaSec = (int)(tHours * 3600.0f);
  }

  // Simple threat score. Later this becomes user-configurable alert policy.
  if (p.approaching && p.cpaEtaSec >= 0 && p.cpaEtaSec <= 900) {
    if (p.cpaDistanceNm <= 2.0f) p.threatLevel = 3;
    else if (p.cpaDistanceNm <= 5.0f) p.threatLevel = 2;
    else if (p.cpaDistanceNm <= 10.0f) p.threatLevel = 1;
  }
  if (p.distanceNm < 3.0f && p.altitudeFt < 5000 && p.threatLevel < 2) p.threatLevel = 2;
}

void SkywatchData::updateBattery() {
  int raw = analogRead(BATTERY_PIN);
  float voltage = (raw / 4095.0f) * 3.3f * BATTERY_DIVIDER_MULTIPLIER;
  float pct = (voltage - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V);
  pct = constrain(pct, 0.0f, 1.0f);
  batteryPercent = pct * 100.0f;
}

bool SkywatchData::tryParseGeoPayload(const String &payload, float &lat, float &lon) {
  DynamicJsonDocument doc(1536);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Geo] JSON error: %s\n", err.c_str());
    return false;
  }

  // ip-api.com: { "status":"success", "lat":.., "lon":.. }
  const char *status = doc["status"] | "";
  if (status[0] && strcmp(status, "success") == 0) {
    lat = doc["lat"].as<float>();
    lon = doc["lon"].as<float>();
    return true;
  }

  // ipwho.is: { "success":true, "latitude":.., "longitude":.. }
  if (doc["success"] | false) {
    if (doc.containsKey("latitude")) {
      lat = doc["latitude"].as<float>();
      lon = doc["longitude"].as<float>();
      return true;
    }
  }

  // ipapi.co: { "latitude":.., "longitude":.. } or error field
  if (!doc["error"] && doc.containsKey("latitude") && doc.containsKey("longitude")) {
    lat = doc["latitude"].as<float>();
    lon = doc["longitude"].as<float>();
    return true;
  }

  Serial.printf("[Geo] Unrecognized payload: %s\n",
                payload.substring(0, min((unsigned)payload.length(), 120u)).c_str());
  return false;
}

void SkywatchData::fetchHomeLocationFromIP() {
  Serial.println(F("[Geo] Fetching approximate location from IP..."));
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[Geo] WiFi not connected"));
    return;
  }

  // Free radar sprite during the request — same heap pressure that broke OpenSky TLS
  // can also make plain HTTPClient fail or return empty bodies on CYD.
  if (beforeNetworkHook) beforeNetworkHook();

  // Free HTTP JSON geo endpoints (no API key). First is the original Skywatch source.
  const char *urls[] = {
    "http://ip-api.com/json/?fields=status,message,lat,lon",
    "http://ipwho.is/",
  };

  bool ok = false;
  float lat = 0.0f;
  float lon = 0.0f;

  for (size_t u = 0; u < sizeof(urls) / sizeof(urls[0]) && !ok; u++) {
    for (int attempt = 1; attempt <= 2 && !ok; attempt++) {
      Serial.printf("[Geo] GET %s (attempt %d) heap=%u\n",
                    urls[u], attempt, ESP.getFreeHeap());

      HTTPClient http;
      http.setConnectTimeout(12000);
      http.setTimeout(12000);
      http.setReuse(false);
      http.useHTTP10(true);

      if (!http.begin(urls[u])) {
        Serial.println(F("[Geo] begin() failed"));
        delay(250);
        continue;
      }

      http.addHeader("Accept", "application/json");
      http.addHeader("User-Agent", "Skywatch-CYD/2.6");
      http.addHeader("Connection", "close");

      int code = http.GET();
      Serial.printf("[Geo] HTTP %d %s\n", code,
                    (code < 0) ? http.errorToString(code).c_str() : "ok");

      if (code != 200) {
        http.end();
        delay(400);
        continue;
      }

      // getString() is more reliable than streaming into ArduinoJson on ESP32.
      String payload = http.getString();
      http.end();
      payload.trim();
      if (payload.length() == 0) {
        Serial.println(F("[Geo] empty body"));
        delay(400);
        continue;
      }

      float pLat = 0.0f, pLon = 0.0f;
      if (!tryParseGeoPayload(payload, pLat, pLon)) {
        delay(400);
        continue;
      }

      // Reject null-island / parse defaults.
      if (fabsf(pLat) < 0.0001f && fabsf(pLon) < 0.0001f) {
        Serial.println(F("[Geo] got 0,0 — ignoring"));
        continue;
      }
      if (pLat < -90.0f || pLat > 90.0f || pLon < -180.0f || pLon > 180.0f) {
        Serial.printf("[Geo] out of range lat=%.4f lon=%.4f\n", pLat, pLon);
        continue;
      }

      lat = pLat;
      lon = pLon;
      ok = true;
    }
  }

  if (ok) {
    homeLat = lat;
    homeLon = lon;
    // Keep coordsOverridden false so this still counts as IP lookup on SYSTEM page.
    Serial.printf("[Geo] Location from IP: lat=%.6f lon=%.6f\n", homeLat, homeLon);
  } else {
    Serial.println(F("[Geo] FAILED — home stays 0,0 until set on web dashboard /coords"));
  }

  if (afterNetworkHook) afterNetworkHook();
}

void SkywatchData::forceOpenSkyRefresh() {
  openSkyManualRequest = true;
}

void SkywatchData::forceTrafficRefresh() {
  openSkyManualRequest = true;
  lastLocalAttemptMs = 0; // allow immediate local ADS-B probe in AUTO mode
  lastApiStatus = "Manual refresh...";
  Serial.println(F("[Traffic] Manual refresh requested"));
}

void SkywatchData::setNetworkHeapHooks(HeapHook beforeNet, HeapHook afterNet) {
  beforeNetworkHook = beforeNet;
  afterNetworkHook = afterNet;
}

unsigned long SkywatchData::openSkyIntervalMs() const {
#if OPENSKY_CALL_MODE == OPENSKY_MODE_60_SEC
  return OPENSKY_60_INTERVAL_MS;
#elif OPENSKY_CALL_MODE == OPENSKY_MODE_360_SEC
  return OPENSKY_360_INTERVAL_MS;
#else
  return 0xFFFFFFFFUL;
#endif
}

bool SkywatchData::openSkyAllowedNow() const {
#if OPENSKY_CALL_MODE == OPENSKY_MODE_MANUAL_ONLY
  return openSkyManualRequest;
#else
  if (openSkyManualRequest || lastOpenSkyAttemptMs == 0) return true;
  return (millis() - lastOpenSkyAttemptMs) >= openSkyIntervalMs();
#endif
}

void SkywatchData::updatePlanes() {
#if SKYWATCH_DATA_SOURCE == DATA_SOURCE_DEMO
  generateDemoTraffic();
  return;
#elif SKYWATCH_DATA_SOURCE == DATA_SOURCE_LOCAL
#if ENABLE_LOCAL_ADSB
  if (localAdsbDueNow() && fetchLocalAdsb()) return;
  if (localAdsbHealthy) return;
#else
  localStatus = "disabled";
#endif
  if (ENABLE_DEMO_FALLBACK && planeCount == 0) generateDemoTraffic();
  return;
#elif SKYWATCH_DATA_SOURCE == DATA_SOURCE_OPENSKY
  if (openSkyAllowedNow() && fetchOpenSky()) return;
  if (ENABLE_DEMO_FALLBACK && planeCount == 0) generateDemoTraffic();
  return;
#else
  // AUTO: keep a healthy local receiver authoritative between rapid polls.
  // When it fails, back off local probing and immediately allow OpenSky fallback.
#if ENABLE_LOCAL_ADSB
  if (localAdsbDueNow()) {
    if (fetchLocalAdsb()) return;
  } else if (localAdsbHealthy) {
    return;
  }
#else
  localStatus = "disabled";
#endif

  if (openSkyAllowedNow() && fetchOpenSky()) return;
  if (ENABLE_DEMO_FALLBACK && planeCount == 0) generateDemoTraffic();
#endif
}

bool SkywatchData::localAdsbDueNow() const {
  if (lastLocalAttemptMs == 0) return true;
  unsigned long interval = localAdsbHealthy
      ? LOCAL_ADSB_INTERVAL_MS
      : LOCAL_ADSB_FAIL_BACKOFF_MS;
  return (millis() - lastLocalAttemptMs) >= interval;
}

void SkywatchData::markLocalFailure(const String &status, int httpCode) {
  localAdsbHealthy = false;
  localStatus = status;
  lastHttpCode = httpCode;
  lastApiStatus = "LOCAL " + status;
  Serial.print(F("[Local ADS-B] failed: "));
  Serial.println(status);
}

bool SkywatchData::fetchLocalAdsb() {
#if !ENABLE_LOCAL_ADSB
  localStatus = "disabled";
  localAdsbHealthy = false;
  return false;
#endif
  lastLocalAttemptMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    markLocalFailure("WiFi down");
    return false;
  }
  if (localAdsbUrl.length() == 0) {
    markLocalFailure("URL missing");
    return false;
  }

  Serial.print(F("[Local ADS-B] Fetching: "));
  Serial.println(localAdsbUrl);

  HTTPClient http;
  http.setConnectTimeout(LOCAL_ADSB_CONNECT_TIMEOUT_MS);
  http.setTimeout(LOCAL_ADSB_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.useHTTP10(true);

  if (!http.begin(localAdsbUrl)) {
    markLocalFailure("begin failed");
    return false;
  }

  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "Skywatch-CYD/2.7");
  http.addHeader("Connection", "close");

  int code = http.GET();
  lastHttpCode = code;
  if (code != 200) {
    String status = (code < 0)
        ? String("HTTP ") + http.errorToString(code)
        : String("HTTP ") + String(code);
    http.end();
    markLocalFailure(status, code);
    return false;
  }

  // Keep only fields Skywatch uses and deserialize directly from the socket.
  // This avoids a full response String plus the old 65KB JSON allocation.
  StaticJsonDocument<512> filter;
  filter["aircraft"][0]["hex"] = true;
  filter["aircraft"][0]["flight"] = true;
  filter["aircraft"][0]["lat"] = true;
  filter["aircraft"][0]["lon"] = true;
  filter["aircraft"][0]["track"] = true;
  filter["aircraft"][0]["gs"] = true;
  filter["aircraft"][0]["seen"] = true;
  filter["aircraft"][0]["seen_pos"] = true;
  filter["aircraft"][0]["alt_baro"] = true;
  filter["aircraft"][0]["alt_geom"] = true;

  DynamicJsonDocument doc(LOCAL_ADSB_JSON_CAPACITY);
  DeserializationError err = deserializeJson(
      doc,
      http.getStream(),
      DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    markLocalFailure(String("JSON ") + err.f_str(), code);
    return false;
  }

  JsonArray aircraft = doc["aircraft"].as<JsonArray>();
  if (aircraft.isNull()) {
    markLocalFailure("No aircraft[]", code);
    return false;
  }

  Plane oldPlanes[MAX_PLANES];
  int oldCount = 0;
  saveOldPlanes(oldPlanes, oldCount);
  clearPlanes();

  int stalePositions = 0;
  for (JsonObject ac : aircraft) {
    if (planeCount >= MAX_PLANES) break;
    if (!ac.containsKey("lat") || !ac.containsKey("lon")) continue;

    float lat = ac["lat"] | NAN;
    float lon = ac["lon"] | NAN;
    if (isnan(lat) || isnan(lon)) continue;
    if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) continue;

    float seenPos = ac["seen_pos"].isNull()
        ? (ac["seen"] | 9999.0f)
        : (ac["seen_pos"] | 9999.0f);
    if (!isfinite(seenPos) || seenPos > LOCAL_ADSB_MAX_POSITION_AGE_SEC) {
      stalePositions++;
      continue;
    }

    float distNm, bearingDeg;
    computeDistanceBearing(homeLat, homeLon, lat, lon, distNm, bearingDeg);
    if (distNm > MAX_RANGE_NM) continue;

    Plane &dst = planes[planeCount];
    dst.icao = String((const char *)(ac["hex"] | ""));
    dst.icao.trim();
    dst.callsign = String((const char *)(ac["flight"] | ""));
    dst.callsign.trim();
    dst.distanceNm = distNm;
    dst.bearingDeg = bearingDeg;
    dst.headingDeg = ac["track"] | 0.0f;
    dst.speedKt = ac["gs"] | 0.0f;
    dst.seenSecondsAgo = (int)seenPos;

    float altFt = 0;
    if (ac["alt_baro"].is<float>() || ac["alt_baro"].is<int>()) {
      altFt = ac["alt_baro"] | 0.0f;
    } else if (ac["alt_geom"].is<float>() || ac["alt_geom"].is<int>()) {
      altFt = ac["alt_geom"] | 0.0f;
    }
    dst.altitudeFt = altFt;
    dst.valid = true;
    planeCount++;
  }

  finishPlaneSet(oldPlanes, oldCount);
  activeSource = "LOCAL";
  localAdsbHealthy = true;
  localStatus = "OK";
  lastHttpCode = code;
  lastApiStatus = planeCount == 0 ? "LOCAL OK, 0" : "LOCAL OK";
  Serial.printf("[Local ADS-B] Loaded %d planes (%d stale positions skipped)\n",
                planeCount, stalePositions);
  return true;
}

bool SkywatchData::fetchOpenSky() {
  openSkyManualRequest = false;
  lastOpenSkyAttemptMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    lastApiStatus = "WiFi not connected";
    lastHttpCode = 0;
    return false;
  }

  float dLat = MAX_RANGE_NM / 60.0f;
  float cosLat = cos(homeLat * PI / 180.0f);
  if (fabs(cosLat) < 0.001f) cosLat = 0.001f;
  float dLon = MAX_RANGE_NM / (60.0f * cosLat);

  // Host + path form gives WiFiClientSecure a reliable SNI target.
  const char *host = "opensky-network.org";
  String uri = String("/api/states/all?lamin=") +
               String(homeLat - dLat, 6) + "&lomin=" + String(homeLon - dLon, 6) +
               "&lamax=" + String(homeLat + dLat, 6) + "&lomax=" + String(homeLon + dLon, 6);

  Serial.println(F("[OpenSky] Fetching planes..."));
  Serial.printf("[OpenSky] https://%s%s\n", host, uri.c_str());
  Serial.printf("[OpenSky] heap free=%u maxAlloc=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Drop large UI allocations for the TLS handshake (radar sprite is ~85KB).
  if (beforeNetworkHook) beforeNetworkHook();

  IPAddress resolved;
  if (WiFi.hostByName(host, resolved)) {
    Serial.print(F("[OpenSky] DNS OK: "));
    Serial.println(resolved);
  } else {
    Serial.println(F("[OpenSky] DNS failed for opensky-network.org"));
    lastApiStatus = "OpenSky DNS fail";
    lastHttpCode = -1;
    if (afterNetworkHook) afterNetworkHook();
    return false;
  }

  int code = -1;
  String payload;

  // Two attempts for flaky TLS. Do NOT retry 429 — that burns the rate limit further.
  for (int attempt = 1; attempt <= 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();              // no CA bundle on device
    client.setHandshakeTimeout(45);    // seconds
    client.setTimeout(20);             // seconds (socket)

    HTTPClient http;
    http.setConnectTimeout(15000);     // ms
    http.setTimeout(20000);            // ms
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setReuse(false);
    http.useHTTP10(true);

    // begin(client, host, port, uri, https)
    if (!http.begin(client, host, 443, uri, true)) {
      Serial.printf("[OpenSky] begin() failed (attempt %d)\n", attempt);
      lastApiStatus = "OpenSky begin failed";
      lastHttpCode = 0;
      http.end();
      delay(400);
      continue;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "Skywatch-CYD/2.6");
    http.addHeader("Connection", "close");

    code = http.GET();
    lastHttpCode = code;
    Serial.printf("[OpenSky] HTTP code: %d (attempt %d) %s\n",
                  code, attempt,
                  (code < 0) ? http.errorToString(code).c_str() : "ok");

    if (code == 200) {
      // Prefer stream read to avoid holding two large copies of the body.
      payload = http.getString();
      http.end();
      break;
    }

    // Keep a readable status for the UI.
    if (code == 429) lastApiStatus = "Rate limited 429";
    else if (code == 403) lastApiStatus = "Forbidden 403";
    else if (code < 0) lastApiStatus = String("TLS/conn ") + http.errorToString(code);
    else lastApiStatus = "OpenSky HTTP " + String(code);

    http.end();

    // Rate limit / hard HTTP errors: one answer is enough.
    if (code == 429 || code == 403 || code == 401) break;
    delay(600);
  }

  if (code != 200) {
    if (afterNetworkHook) afterNetworkHook();
    return false;
  }

  payload.trim();
  if (payload.length() == 0 || payload[0] != '{') {
    lastApiStatus = "OpenSky non-JSON";
    if (afterNetworkHook) afterNetworkHook();
    return false;
  }

  // Local 20nm boxes are small (often <2KB). 16KB is plenty and leaves RAM for UI.
  bool ok = false;
  {
    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      ok = applyOpenSkyStates(doc.as<JsonVariant>());
    } else {
      // One more try with a larger doc if the box was unusually busy.
      DynamicJsonDocument docBig(32768);
      err = deserializeJson(docBig, payload);
      if (err) {
        lastApiStatus = String("OpenSky JSON ") + err.f_str();
        ok = false;
      } else {
        ok = applyOpenSkyStates(docBig.as<JsonVariant>());
      }
    }
  }

  if (afterNetworkHook) afterNetworkHook();
  return ok;
}

bool SkywatchData::applyOpenSkyStates(JsonVariant root) {
  JsonArray states = root["states"].as<JsonArray>();
  if (states.isNull()) {
    lastApiStatus = "OpenSky no states";
    return false;
  }

  Plane oldPlanes[MAX_PLANES];
  int oldCount = 0;
  saveOldPlanes(oldPlanes, oldCount);
  clearPlanes();

  for (JsonVariant v : states) {
    if (!v.is<JsonArray>() || planeCount >= MAX_PLANES) continue;
    JsonArray s = v.as<JsonArray>();
    double lon = s[5] | NAN;
    double lat = s[6] | NAN;
    if (isnan(lat) || isnan(lon)) continue;

    float distNm, bearingDeg;
    computeDistanceBearing(homeLat, homeLon, (float)lat, (float)lon, distNm, bearingDeg);
    if (distNm > MAX_RANGE_NM) continue;

    Plane &dst = planes[planeCount];
    dst.icao = String((const char *)(s[0] | ""));
    dst.icao.trim();
    dst.callsign = String((const char *)(s[1] | ""));
    dst.callsign.trim();
    dst.distanceNm = distNm;
    dst.bearingDeg = bearingDeg;
    dst.altitudeFt = (float)(s[7] | 0.0) * 3.28084f;
    dst.speedKt = (float)(s[9] | 0.0) * 1.94384f;
    dst.headingDeg = s[10] | 0.0;
    int lastContact = s[4] | 0;
    dst.seenSecondsAgo = lastContact > 0 ? (int)(time(nullptr) - lastContact) : 0;
    dst.valid = true;
    planeCount++;
  }

  finishPlaneSet(oldPlanes, oldCount);
  activeSource = "OPENSKY";
  lastApiStatus = planeCount == 0 ? "OpenSky OK, 0" : "OpenSky OK";
  Serial.printf("[OpenSky] Loaded %d planes\n", planeCount);
  return true;
}

void SkywatchData::generateDemoTraffic() {
  Plane oldPlanes[MAX_PLANES];
  int oldCount = 0;
  saveOldPlanes(oldPlanes, oldCount);
  clearPlanes();

  float t = millis() / 1000.0f;
  struct DemoSeed { const char* call; const char* icao; float baseDist; float baseBearing; float speed; float alt; float hdgOffset; };
  DemoSeed seeds[] = {
    {"DEMO101", "D00101", 7.0f,  35.0f, 420, 32000, 180},
    {"TEST22",  "D00022", 13.0f, 125.0f, 310, 12000, 260},
    {"SKY7",    "D00007", 18.0f, 300.0f, 455, 38000,  90},
  };

  int demoCount = 3;
  for (int i = 0; i < demoCount && i < MAX_PLANES; i++) {
    Plane &p = planes[planeCount];
    p.callsign = seeds[i].call;
    p.icao = seeds[i].icao;
    p.distanceNm = seeds[i].baseDist + sin(t * 0.08f + i) * 2.0f;
    p.distanceNm = constrain(p.distanceNm, 1.5f, MAX_RANGE_NM - 0.5f);
    p.bearingDeg = fmodf(seeds[i].baseBearing + t * (4.0f + i * 1.7f), 360.0f);
    p.headingDeg = fmodf(p.bearingDeg + seeds[i].hdgOffset, 360.0f);
    p.speedKt = seeds[i].speed;
    p.altitudeFt = seeds[i].alt;
    p.seenSecondsAgo = 0;
    p.valid = true;
    planeCount++;
  }

  finishPlaneSet(oldPlanes, oldCount);
  activeSource = "DEMO";
  if (lastApiStatus.indexOf("Rate limited") >= 0) lastApiStatus = "DEMO after 429";
  else lastApiStatus = "DEMO MODE";
  if (lastHttpCode == 0) lastHttpCode = -1;
}

void SkywatchData::computeDistanceBearing(float lat1, float lon1, float lat2, float lon2,
                                          float &distanceNm, float &bearingDeg) {
  const float R_earth_km = 6371.0f;
  float phi1 = lat1 * PI / 180.0f;
  float phi2 = lat2 * PI / 180.0f;
  float dPhi = (lat2 - lat1) * PI / 180.0f;
  float dLam = (lon2 - lon1) * PI / 180.0f;
  float a = sin(dPhi / 2) * sin(dPhi / 2) + cos(phi1) * cos(phi2) * sin(dLam / 2) * sin(dLam / 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  distanceNm = (R_earth_km * c) * 0.539957f;
  float y = sin(dLam) * cos(phi2);
  float x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLam);
  float brng = atan2(y, x) * 180.0f / PI;
  bearingDeg = fmodf((brng + 360.0f), 360.0f);
}

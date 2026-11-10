#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"

struct TrailPoint {
  float distanceNm = 0;
  float bearingDeg = 0;
  bool valid = false;
};

struct Plane {
  String icao;
  float distanceNm = 0;
  float bearingDeg = 0;
  float headingDeg = 0;
  float altitudeFt = 0;
  float speedKt = 0;
  int seenSecondsAgo = 0;
  String callsign;
  TrailPoint trail[TRAIL_POINTS];

  // Build 24 awareness fields. These are computed from bearing/range + ground track/speed.
  float cpaDistanceNm = 0;
  int cpaEtaSec = -1;
  float closingRateKt = 0;
  bool approaching = false;
  uint8_t threatLevel = 0; // 0 none, 1 low, 2 medium, 3 high

  bool valid = false;
};


struct SkyEvent {
  String type;
  String callsign;
  String icao;
  float distanceNm = 0;
  float cpaNm = 0;
  int etaSec = -1;
  uint8_t threatLevel = 0;
  unsigned long createdMs = 0;
  bool logged = false;
  bool mqttPublished = false;
};

class SkywatchData {
public:
  using HeapHook = void (*)();

  void begin();
  void fetchHomeLocationFromIP();
  void updatePlanes();
  void updateBattery();
  void forceOpenSkyRefresh();
  // Immediate traffic pull: bypass OpenSky interval and local ADS-B backoff.
  void forceTrafficRefresh();
  // Optional: free large UI buffers around HTTPS (radar sprite ~85KB).
  void setNetworkHeapHooks(HeapHook beforeNet, HeapHook afterNet);

  void setHomeCoords(float lat, float lon, bool overridden = true);
  bool loadSavedHomeCoords();
  bool saveHomeCoords(float lat, float lon);
  bool clearSavedHomeCoords();
  float getHomeLat() const;
  float getHomeLon() const;
  bool coordsWereOverridden() const;
  String getHomeLocationSource() const;

  int getPlaneCount() const;
  const Plane& getPlane(int index) const;

  float getBatteryPercent() const;
  unsigned long getLastPlaneUpdateMs() const;
  String getLastApiStatus() const;
  int getLastHttpCode() const;
  String getActiveSourceLabel() const;
  String getLocalStatus() const;
  String getLocalAdsbUrl() const;
  void setLocalAdsbUrl(const String &url);
  String getOpenSkyModeLabel() const;

  int getEventCount() const;
  const SkyEvent& getEvent(int index) const;
  const SkyEvent& getLatestEvent() const;
  bool hasRecentAlert(unsigned long maxAgeMs) const;
  String eventLogLine(int index) const;
  void markEventLogged(int index);
  void markEventMqttPublished(int index);

  void computeDistanceBearing(float lat1, float lon1, float lat2, float lon2,
                              float &distanceNm, float &bearingDeg);

private:
  Plane planes[MAX_PLANES];
  int planeCount = 0;
  SkyEvent events[20];
  int eventCount = 0;

  float homeLat = 0.0f;
  float homeLon = 0.0f;
  bool coordsOverridden = false;
  String homeLocationSource = "unset";

  float batteryPercent = 100.0f;
  unsigned long lastPlaneUpdateMs = 0;
  String lastApiStatus = "Not fetched yet";
  int lastHttpCode = 0;
  String activeSource = "BOOT";
  String localStatus = "Not tried";
  String localAdsbUrl = LOCAL_ADSB_URL_DEFAULT;
  bool localAdsbHealthy = false;

  unsigned long lastLocalAttemptMs = 0;
  unsigned long lastOpenSkyAttemptMs = 0;
  bool openSkyManualRequest = false;
  HeapHook beforeNetworkHook = nullptr;
  HeapHook afterNetworkHook = nullptr;

  void clearPlanes();
  int findOldPlaneIndex(const Plane oldPlanes[], int oldCount, const String &icao, const String &callsign) const;
  void inheritTrailFromOld(Plane &dst, const Plane &oldPlane);
  void saveOldPlanes(Plane oldPlanes[], int &oldCount) const;
  void finishPlaneSet(Plane oldPlanes[], int oldCount);
  void computeAwareness(Plane &p);
  void addEvent(const String &type, const Plane &p);

  bool fetchLocalAdsb();
  bool localAdsbDueNow() const;
  void markLocalFailure(const String &status, int httpCode = 0);
  bool fetchOpenSky();
  bool applyOpenSkyStates(JsonVariant root);
  bool tryParseGeoPayload(const String &payload, float &lat, float &lon);
  void generateDemoTraffic();
  bool openSkyAllowedNow() const;
  unsigned long openSkyIntervalMs() const;
};

#include "SkywatchMqtt.h"
#include <ArduinoJson.h>
#include "SkywatchData.h"

SkywatchMqtt::SkywatchMqtt() : client(net) {}

String SkywatchMqtt::sanitizeId(const String &raw, const char *fallback) const {
  String id = raw;
  id.trim();
  id.replace(" ", "_");
  id.replace("/", "_");
  id.replace("#", "_");
  id.replace("+", "_");
  if (!id.length()) id = fallback;
  return id;
}

void SkywatchMqtt::rebuildTopics() {
  alertsTopic = String("workshop/skywatch/") + nodeId + "/alerts";
  planesTopic = String("workshop/skywatch/") + nodeId + "/planes";
  clientId = String("skywatch-") + nodeId;
}

void SkywatchMqtt::setBroker(const String &host) {
  String value = host;
  value.trim();
  if (value.length()) broker = value;
}

void SkywatchMqtt::setPort(int value) {
  if (value > 0 && value <= 65535) port = (uint16_t)value;
}

void SkywatchMqtt::setGatewayId(const String &id) {
  gatewayId = sanitizeId(id, MQTT_GATEWAY_ID_DEFAULT);
}

void SkywatchMqtt::setNodeId(const String &id) {
  nodeId = sanitizeId(id, MQTT_NODE_ID_DEFAULT);
  rebuildTopics();
}

void SkywatchMqtt::begin() {
  rebuildTopics();
  net.setTimeout(2);
#if defined(ESP32)
  net.setConnectionTimeout(2000);
#endif
  if (!client.setBufferSize(MQTT_PAYLOAD_BUFFER)) {
    Serial.printf("[MQTT] Warning: could not set MQTT buffer to %u bytes\n", MQTT_PAYLOAD_BUFFER);
  }
  client.setSocketTimeout(2);
  client.setKeepAlive(15);
  client.setServer(broker.c_str(), port);
  Serial.printf("[MQTT] Ready (%s:%u)\n", broker.c_str(), port);
  Serial.printf("[MQTT] Alerts: %s\n", alertsTopic.c_str());
  Serial.printf("[MQTT] Planes: %s\n", planesTopic.c_str());
  lastReconnectAttemptMs = 0;
  reconnectBackoffMs = MQTT_RECONNECT_MIN_MS;
}

void SkywatchMqtt::tryReconnect() {
  const unsigned long now = millis();
  if (lastReconnectAttemptMs != 0 && (now - lastReconnectAttemptMs) < reconnectBackoffMs) {
    return;
  }
  lastReconnectAttemptMs = now;

  Serial.printf("[MQTT] Connecting to %s:%u as %s\n", broker.c_str(), port, clientId.c_str());
  // A single connect() attempt; backoff avoids retrying inside the UI loop.
  const bool ok = client.connect(clientId.c_str());
  if (ok) {
    Serial.println(F("[MQTT] Connected"));
    reconnectBackoffMs = MQTT_RECONNECT_MIN_MS;
    lastPlaneSnapshotMs = 0;
    return;
  }

  Serial.printf("[MQTT] Connect failed, state=%d; retry in %lu ms\n",
                client.state(), reconnectBackoffMs);
  unsigned long next = reconnectBackoffMs * 2UL;
  if (next > MQTT_RECONNECT_MAX_MS) next = MQTT_RECONNECT_MAX_MS;
  reconnectBackoffMs = next;
}

bool SkywatchMqtt::isAlertType(const String &type) const {
  return type == "NEW_TARGET" || type == "APPROACHING" || type == "THREAT_UP";
}

bool SkywatchMqtt::publishAlert(const SkyEvent &event) {
  DynamicJsonDocument doc(512);
  doc["gateway_id"] = gatewayId;
  doc["node_id"] = nodeId;
  doc["type"] = event.type;
  doc["callsign"] = event.callsign;
  doc["icao"] = event.icao;
  doc["distanceNm"] = event.distanceNm;
  doc["cpaNm"] = event.cpaNm;
  doc["etaSec"] = event.etaSec;
  doc["threatLevel"] = event.threatLevel;

  String payload;
  serializeJson(doc, payload);
  if (!payload.length()) return false;
  if (!client.publish(alertsTopic.c_str(), payload.c_str(), false)) {
    Serial.println(F("[MQTT] Alert publish failed"));
    return false;
  }
  Serial.printf("[MQTT] Alert %s -> %s\n", event.type.c_str(), alertsTopic.c_str());
  return true;
}

void SkywatchMqtt::publishPendingAlerts(SkywatchData &data) {
  int published = 0;
  // Oldest unpublished first so a burst keeps chronological order on the broker.
  for (int i = data.getEventCount() - 1; i >= 0; i--) {
    const SkyEvent &event = data.getEvent(i);
    if (event.mqttPublished) continue;
    if (!isAlertType(event.type)) {
      data.markEventMqttPublished(i);
      continue;
    }
    if (!publishAlert(event)) break;
    data.markEventMqttPublished(i);
    published++;
    if (published >= MQTT_ALERTS_PER_UPDATE) break;
  }
}

bool SkywatchMqtt::publishPlanes(SkywatchData &data) {
  DynamicJsonDocument doc(768 + MAX_PLANES * 220);
  doc["gateway_id"] = gatewayId;
  doc["node_id"] = nodeId;
  doc["source"] = data.getActiveSourceLabel();
  doc["count"] = data.getPlaneCount();

  JsonArray arr = doc.createNestedArray("planes");
  for (int i = 0; i < data.getPlaneCount(); i++) {
    const Plane &p = data.getPlane(i);
    JsonObject o = arr.createNestedObject();
    o["callsign"] = p.callsign;
    o["icao"] = p.icao;
    o["distanceNm"] = p.distanceNm;
    o["bearingDeg"] = p.bearingDeg;
    o["headingDeg"] = p.headingDeg;
    o["altitudeFt"] = p.altitudeFt;
    o["speedKt"] = p.speedKt;
    o["cpaNm"] = p.cpaDistanceNm;
    o["threatLevel"] = p.threatLevel;
  }

  String payload;
  serializeJson(doc, payload);
  if (!payload.length()) return false;
  if (payload.length() + planesTopic.length() + 16 > MQTT_PAYLOAD_BUFFER) {
    Serial.printf("[MQTT] Plane snapshot too large (%u bytes)\n", (unsigned)payload.length());
    return false;
  }
  if (!client.publish(planesTopic.c_str(), payload.c_str(), false)) {
    Serial.println(F("[MQTT] Plane snapshot publish failed"));
    return false;
  }
  Serial.printf("[MQTT] Planes %d -> %s\n", data.getPlaneCount(), planesTopic.c_str());
  return true;
}

void SkywatchMqtt::maybePublishPlanes(SkywatchData &data) {
  const unsigned long now = millis();
  if (lastPlaneSnapshotMs != 0 && (now - lastPlaneSnapshotMs) < MQTT_PLANE_SNAPSHOT_INTERVAL_MS) {
    return;
  }
  if (publishPlanes(data)) lastPlaneSnapshotMs = now;
}

void SkywatchMqtt::update(SkywatchData &data) {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!client.connected()) {
    tryReconnect();
  }
  if (!client.connected()) return;

  client.loop();
  publishPendingAlerts(data);
  maybePublishPlanes(data);
}

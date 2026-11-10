#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

class SkywatchData;
struct SkyEvent;

// Publishes Skywatch alerts and plane snapshots over plain MQTT (no TLS).
// Reconnect uses a single attempt plus backoff so the radar UI loop is not blocked.
class SkywatchMqtt {
public:
  SkywatchMqtt();

  void setBroker(const String &host);
  void setPort(int port);
  void setGatewayId(const String &id);
  void setNodeId(const String &id);

  void begin();
  void update(SkywatchData &data);

private:
  WiFiClient net;
  PubSubClient client;
  String broker = MQTT_BROKER_DEFAULT;
  uint16_t port = MQTT_PORT_DEFAULT;
  String gatewayId = MQTT_GATEWAY_ID_DEFAULT;
  String nodeId = MQTT_NODE_ID_DEFAULT;
  String clientId;
  String alertsTopic;
  String planesTopic;
  unsigned long lastReconnectAttemptMs = 0;
  unsigned long reconnectBackoffMs = MQTT_RECONNECT_MIN_MS;
  unsigned long lastPlaneSnapshotMs = 0;

  void rebuildTopics();
  String sanitizeId(const String &raw, const char *fallback) const;
  void tryReconnect();
  bool isAlertType(const String &type) const;
  bool publishAlert(const SkyEvent &event);
  void publishPendingAlerts(SkywatchData &data);
  bool publishPlanes(SkywatchData &data);
  void maybePublishPlanes(SkywatchData &data);
};

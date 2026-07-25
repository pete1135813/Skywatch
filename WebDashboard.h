#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "SkywatchData.h"

class WebDashboard {
public:
  void begin(SkywatchData *data);
  void handleClient();

private:
  WebServer server = WebServer(80);
  SkywatchData *skywatch = nullptr;

  void handleRoot();
  void handlePlanes();
  void handleCoords();
  void handleCoordsReset();
};

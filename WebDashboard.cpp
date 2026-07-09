#include "WebDashboard.h"

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <title>Skywatch Dashboard</title>
  <meta charset="utf-8" />
  <style>
    body { background:#000; color:#0f0; font-family:Arial, sans-serif; text-align:center; }
    #radar { background:#000; border:1px solid #0f0; border-radius:50%; margin-top:10px; }
    .coord-box { margin-top:10px; color:#0f0; }
    input { background:#111; color:#0f0; border:1px solid #0f0; padding:4px; margin:2px; }
    button { background:#0f0; color:#000; border:none; padding:5px 10px; cursor:pointer; }
    button:hover { background:#9f9; }
  </style>
</head>
<body>
  <h2>Skywatch Radar</h2>
  <canvas id="radar" width="300" height="300"></canvas>

  <div class="coord-box">
    <div>Current: <span id="currentCoords">lat=0, lon=0</span></div>
    <div style="margin-top:6px;">
      New lat: <input id="lat" size="8" />
      New lon: <input id="lon" size="8" />
      <button onclick="updateCoords()">Update</button>
    </div>
  </div>

  <script>
    const radar = document.getElementById('radar');
    const ctx = radar.getContext('2d');
    const MAX_RANGE_NM = 20.0;

    function drawRadarBackground() {
      const cx = radar.width / 2;
      const cy = radar.height / 2;
      const r  = 130;
      ctx.clearRect(0, 0, radar.width, radar.height);
      ctx.strokeStyle = '#0f0';
      for (const scale of [1, 0.75, 0.5, 0.25]) {
        ctx.beginPath(); ctx.arc(cx, cy, r * scale, 0, 2*Math.PI); ctx.stroke();
      }
      ctx.beginPath(); ctx.moveTo(cx-r, cy); ctx.lineTo(cx+r, cy); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(cx, cy-r); ctx.lineTo(cx, cy+r); ctx.stroke();
      ctx.fillStyle = '#0f0'; ctx.beginPath(); ctx.arc(cx, cy, 3, 0, 2*Math.PI); ctx.fill();
    }

    function altitudeColor(ft) {
      if (ft < 5000) return '#0f0';
      if (ft < 15000) return '#ff0';
      if (ft < 30000) return '#fa0';
      return '#f00';
    }

    async function fetchPlanes() {
      try {
        const res = await fetch('/planes');
        if (!res.ok) return;
        const data = await res.json();
        const cx = radar.width / 2;
        const cy = radar.height / 2;
        const rMax = 130;
        drawRadarBackground();
        document.getElementById('currentCoords').textContent =
          `lat=${data.homeLat.toFixed(4)}, lon=${data.homeLon.toFixed(4)}`;

        for (const p of data.planes) {
          const bearing = p.bearingDeg * Math.PI / 180.0;
          const r = Math.min(rMax, (p.distanceNm / MAX_RANGE_NM) * rMax);
          const x = cx + r * Math.sin(bearing);
          const y = cy - r * Math.cos(bearing);
          const col = altitudeColor(p.altitudeFt);
          const head = p.headingDeg * Math.PI / 180.0;
          const len = 10;
          const x2 = x + len * Math.sin(head);
          const y2 = y - len * Math.cos(head);
          ctx.strokeStyle = col; ctx.fillStyle = col;
          ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x2, y2); ctx.stroke();
          ctx.beginPath(); ctx.arc(x, y, 2, 0, 2*Math.PI); ctx.fill();
          ctx.font = '10px Arial';
          ctx.fillText(p.callsign || 'AC', x2 + 4, y2);
        }
      } catch (e) { console.error(e); }
    }

    async function updateCoords() {
      const lat = parseFloat(document.getElementById('lat').value);
      const lon = parseFloat(document.getElementById('lon').value);
      if (isNaN(lat) || isNaN(lon)) { alert('Invalid lat/lon'); return; }
      await fetch('/coords', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({lat, lon})
      });
      fetchPlanes();
    }

    drawRadarBackground();
    fetchPlanes();
    setInterval(fetchPlanes, 5000);
  </script>
</body>
</html>
)HTML";

void WebDashboard::begin(SkywatchData *data) {
  skywatch = data;
  server.on("/", HTTP_GET, [this]() { handleRoot(); });
  server.on("/planes", HTTP_GET, [this]() { handlePlanes(); });
  server.on("/coords", HTTP_POST, [this]() { handleCoords(); });
  server.begin();
  Serial.println("[Server] HTTP server started.");
}

void WebDashboard::handleClient() {
  server.handleClient();
}

void WebDashboard::handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void WebDashboard::handlePlanes() {
  if (!skywatch) {
    server.send(500, "application/json", "{\"error\":\"skywatch null\"}");
    return;
  }

  DynamicJsonDocument doc(2048 + MAX_PLANES * 128);
  doc["homeLat"] = skywatch->getHomeLat();
  doc["homeLon"] = skywatch->getHomeLon();

  JsonArray arr = doc.createNestedArray("planes");
  for (int i = 0; i < skywatch->getPlaneCount(); i++) {
    const Plane &p = skywatch->getPlane(i);
    JsonObject o = arr.createNestedObject();
    o["distanceNm"] = p.distanceNm;
    o["bearingDeg"] = p.bearingDeg;
    o["headingDeg"] = p.headingDeg;
    o["altitudeFt"] = p.altitudeFt;
    o["callsign"] = p.callsign;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void WebDashboard::handleCoords() {
  if (!skywatch) {
    server.send(500, "text/plain", "skywatch null");
    return;
  }

  String body = server.arg("plain");
  if (!body.length()) {
    server.send(400, "text/plain", "Empty body");
    return;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server.send(400, "text/plain", String("JSON parse error: ") + err.c_str());
    return;
  }

  float lat = doc["lat"] | skywatch->getHomeLat();
  float lon = doc["lon"] | skywatch->getHomeLon();
  skywatch->setHomeCoords(lat, lon, true);

  Serial.printf("[Coords] Overridden to lat=%.6f lon=%.6f\n", lat, lon);
  server.send(200, "text/plain", "OK");
}

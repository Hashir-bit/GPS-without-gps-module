import http.server
import socketserver
import json
import os
import math
import time
import socket

PORT = 8000

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

LOCAL_IP = get_local_ip()

# Global tracker telemetry state
telemetry = {
    "lat": 21.1255,          # Default fallback origin (Nagpur)
    "lng": 79.0522,
    "accuracy": 12.0,
    "step_count": 0,
    "heading": 0.0,
    "offset_x": 0.0,
    "offset_y": 0.0,
    "accel": 9.81,
    "status": "ONLINE",
    "last_update": time.time()
}

HTML_DASHBOARD = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>St. Vincent Pallotti | GPS-Less ESP32 Telemetry Dashboard</title>

  <!-- Google Fonts & Leaflet CSS -->
  <link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@400;500;600;700;800&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <!-- Lucide Icons -->
  <script src="https://unpkg.com/lucide@latest"></script>

  <style>
    :root {
      /* Sleek Modern Light / Vibrant Slate Palette (Judges Choice) */
      --bg-gradient: linear-gradient(135deg, #f0f4f8 0%, #e2e8f0 100%);
      --card-bg: rgba(255, 255, 255, 0.85);
      --card-border: rgba(203, 213, 225, 0.8);
      --accent-primary: #2563eb;    /* Vibrant Royal Blue */
      --accent-cyan: #0284c7;       /* Tech Sky Blue */
      --accent-emerald: #059669;    /* Emerald Green */
      --accent-gold: #d97706;       /* Warm Amber */
      --accent-purple: #7c3aed;     /* Royal Purple */
      --text-main: #0f172a;         /* Deep Slate Main Text */
      --text-muted: #64748b;        /* Soft Slate Subtext */
      --font-sans: 'Plus Jakarta Sans', sans-serif;
      --font-mono: 'JetBrains Mono', monospace;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: var(--font-sans);
      background: var(--bg-gradient);
      color: var(--text-main);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      overflow-x: hidden;
    }

    /* Top Navigation Header */
    header {
      background: rgba(255, 255, 255, 0.92);
      backdrop-filter: blur(16px);
      border-bottom: 1px solid rgba(226, 232, 240, 0.8);
      padding: 14px 28px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      position: sticky;
      top: 0;
      z-index: 1000;
      box-shadow: 0 4px 20px rgba(0, 0, 0, 0.04);
    }

    .brand {
      display: flex;
      align-items: center;
      gap: 14px;
    }

    .brand-logo {
      width: 44px;
      height: 44px;
      background: linear-gradient(135deg, var(--accent-primary), var(--accent-purple));
      border-radius: 12px;
      display: flex;
      align-items: center;
      justify-content: center;
      box-shadow: 0 6px 16px rgba(37, 99, 235, 0.25);
      color: #fff;
    }

    .brand-text h1 {
      font-size: 17px;
      font-weight: 800;
      color: var(--text-main);
      letter-spacing: -0.3px;
    }

    .brand-text p {
      font-size: 12px;
      color: var(--accent-primary);
      font-weight: 600;
    }

    .header-actions {
      display: flex;
      align-items: center;
      gap: 14px;
    }

    .team-btn {
      background: linear-gradient(135deg, var(--accent-primary), #1d4ed8);
      color: #fff;
      font-weight: 700;
      font-size: 13px;
      padding: 9px 20px;
      border: none;
      border-radius: 30px;
      cursor: pointer;
      display: flex;
      align-items: center;
      gap: 8px;
      transition: all 0.3s ease;
      box-shadow: 0 4px 14px rgba(37, 99, 235, 0.3);
    }

    .team-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 8px 20px rgba(37, 99, 235, 0.4);
    }

    .status-badge {
      display: flex;
      align-items: center;
      gap: 10px;
      background: #f1f5f9;
      border: 1px solid #cbd5e1;
      padding: 8px 16px;
      border-radius: 30px;
      font-size: 13px;
      font-weight: 700;
      color: var(--text-main);
    }

    .status-dot {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: var(--accent-emerald);
      box-shadow: 0 0 10px var(--accent-emerald);
      animation: pulse 1.8s infinite;
    }

    .status-dot.offline {
      background: #ef4444;
      box-shadow: 0 0 10px #ef4444;
    }

    @keyframes pulse {
      0%, 100% { transform: scale(1); opacity: 1; }
      50% { transform: scale(1.35); opacity: 0.75; }
    }

    /* Dashboard Layout Grid */
    .dashboard-grid {
      display: grid;
      grid-template-columns: 360px 1fr;
      gap: 24px;
      padding: 24px;
      max-width: 1600px;
      margin: 0 auto;
      width: 100%;
      flex: 1;
    }

    @media (max-width: 1024px) {
      .dashboard-grid { grid-template-columns: 1fr; }
    }

    /* Light Glassmorphism Card Style */
    .glass-card {
      background: var(--card-bg);
      backdrop-filter: blur(16px);
      border: 1px solid var(--card-border);
      border-radius: 24px;
      padding: 24px;
      box-shadow: 0 10px 30px rgba(15, 23, 42, 0.05);
      display: flex;
      flex-direction: column;
      gap: 20px;
    }

    .card-title {
      font-size: 13px;
      font-weight: 800;
      text-transform: uppercase;
      letter-spacing: 0.8px;
      color: var(--text-muted);
      display: flex;
      align-items: center;
      gap: 8px;
    }

    /* Telemetry Metrics List */
    .metrics-list {
      display: flex;
      flex-direction: column;
      gap: 12px;
    }

    .metric-item {
      background: #ffffff;
      border: 1px solid #e2e8f0;
      border-radius: 16px;
      padding: 14px 16px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      box-shadow: 0 2px 6px rgba(0, 0, 0, 0.02);
    }

    .metric-info {
      display: flex;
      align-items: center;
      gap: 14px;
    }

    .metric-icon {
      width: 40px;
      height: 40px;
      border-radius: 12px;
      background: rgba(37, 99, 235, 0.08);
      color: var(--accent-primary);
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .metric-label {
      font-size: 12px;
      color: var(--text-muted);
      font-weight: 500;
    }

    .metric-value {
      font-family: var(--font-mono);
      font-size: 17px;
      font-weight: 700;
      color: var(--text-main);
    }

    /* Map Box Container */
    .map-card {
      position: relative;
      overflow: hidden;
      min-height: 600px;
      padding: 0;
      border-radius: 24px;
    }

    #map {
      width: 100%;
      height: 100%;
      border-radius: 24px;
      z-index: 1;
      background: #e2e8f0;
    }

    .map-overlay-header {
      position: absolute;
      top: 16px;
      left: 16px;
      right: 16px;
      z-index: 500;
      display: flex;
      justify-content: space-between;
      pointer-events: none;
    }

    .map-pill {
      pointer-events: auto;
      background: rgba(255, 255, 255, 0.92);
      backdrop-filter: blur(12px);
      border: 1px solid #cbd5e1;
      padding: 8px 18px;
      border-radius: 30px;
      font-size: 13px;
      font-weight: 700;
      color: var(--text-main);
      display: flex;
      align-items: center;
      gap: 10px;
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.06);
    }

    /* Node Target Marker Animation */
    .node-marker {
      background: var(--accent-primary);
      width: 20px;
      height: 20px;
      border-radius: 50%;
      border: 3px solid #fff;
      box-shadow: 0 0 20px rgba(37, 99, 235, 0.8);
      animation: pulse-ring 2s infinite;
    }

    @keyframes pulse-ring {
      0% { box-shadow: 0 0 0 0 rgba(37, 99, 235, 0.7); }
      70% { box-shadow: 0 0 0 18px rgba(37, 99, 235, 0); }
      100% { box-shadow: 0 0 0 0 rgba(37, 99, 235, 0); }
    }

    /* Modal Overlay for Team Info */
    .modal-overlay {
      position: fixed;
      top: 0; left: 0; right: 0; bottom: 0;
      background: rgba(15, 23, 42, 0.6);
      backdrop-filter: blur(8px);
      display: none;
      align-items: center;
      justify-content: center;
      z-index: 2000;
    }

    .modal-card {
      background: #ffffff;
      border: 1px solid var(--accent-primary);
      box-shadow: 0 20px 50px rgba(37, 99, 235, 0.2);
      border-radius: 28px;
      width: 90%;
      max-width: 560px;
      padding: 30px;
      display: flex;
      flex-direction: column;
      gap: 20px;
      animation: modalSlide 0.3s ease-out;
    }

    @keyframes modalSlide {
      from { transform: translateY(30px); opacity: 0; }
      to { transform: translateY(0); opacity: 1; }
    }

    .modal-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      border-bottom: 1px solid #e2e8f0;
      padding-bottom: 16px;
    }

    .modal-header h2 {
      font-size: 20px;
      color: var(--text-main);
      display: flex;
      align-items: center;
      gap: 10px;
      font-weight: 800;
    }

    .close-btn {
      background: #f1f5f9;
      border: none;
      color: var(--text-muted);
      font-size: 22px;
      width: 36px;
      height: 36px;
      border-radius: 50%;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .close-btn:hover { background: #e2e8f0; color: var(--text-main); }

    .team-member {
      background: #f8fafc;
      border: 1px solid #e2e8f0;
      border-radius: 18px;
      padding: 16px;
      display: flex;
      align-items: center;
      gap: 16px;
      transition: transform 0.2s ease;
    }
    .team-member:hover {
      transform: translateY(-2px);
      box-shadow: 0 4px 12px rgba(0,0,0,0.04);
    }

    .member-avatar {
      width: 48px;
      height: 48px;
      border-radius: 50%;
      background: linear-gradient(135deg, var(--accent-primary), var(--accent-cyan));
      color: #fff;
      font-weight: 800;
      font-size: 17px;
      display: flex;
      align-items: center;
      justify-content: center;
      box-shadow: 0 4px 10px rgba(37, 99, 235, 0.25);
    }

    .member-details h3 {
      font-size: 16px;
      color: var(--text-main);
      font-weight: 700;
    }

    .member-details p {
      font-size: 13px;
      color: var(--text-muted);
      display: flex;
      align-items: center;
      gap: 6px;
      margin-top: 4px;
      font-weight: 500;
    }

    footer {
      text-align: center;
      padding: 16px;
      font-size: 13px;
      color: var(--text-muted);
      border-top: 1px solid rgba(226, 232, 240, 0.8);
      font-weight: 500;
    }
  </style>
</head>
<body>

  <!-- Header Navigation -->
  <header>
    <div class="brand">
      <div class="brand-logo"><i data-lucide="navigation"></i></div>
      <div class="brand-text">
        <h1>St. Vincent Pallotti College of Engg. & Tech.</h1>
        <p>Participating in VNIT Nagpur Ideathon 2026 | ESP32 GPS-Less Tracker</p>
      </div>
    </div>
    
    <div class="header-actions">
      <button class="team-btn" onclick="toggleModal(true)"><i data-lucide="users"></i> Team Members</button>
      <div class="status-badge">
        <div class="status-dot" id="statusDot"></div>
        <span id="statusText">RECEIVING TELEMETRY</span>
      </div>
    </div>
  </header>

  <!-- Main Dashboard Grid -->
  <div class="dashboard-grid">
    
    <!-- Left Sidebar Telemetry Cards -->
    <div class="glass-card">
      <div class="card-title"><i data-lucide="activity"></i> Inertial Dead Reckoning (PDR)</div>
      
      <div class="metrics-list">
        <div class="metric-item">
          <div class="metric-info">
            <div class="metric-icon"><i data-lucide="footprints"></i></div>
            <div>
              <div class="metric-label">Step Count</div>
              <div class="metric-value" id="valSteps">0</div>
            </div>
          </div>
          <span style="font-size: 11px; font-weight: 700; color: var(--accent-primary)">PDR Active</span>
        </div>

        <div class="metric-item">
          <div class="metric-info">
            <div class="metric-icon"><i data-lucide="compass"></i></div>
            <div>
              <div class="metric-label">Heading Angle</div>
              <div class="metric-value" id="valHeading">0.0°</div>
            </div>
          </div>
        </div>

        <div class="metric-item">
          <div class="metric-info">
            <div class="metric-icon"><i data-lucide="move"></i></div>
            <div>
              <div class="metric-label">Offset (East / North)</div>
              <div class="metric-value" id="valOffset">0.0m / 0.0m</div>
            </div>
          </div>
        </div>

        <div class="metric-item">
          <div class="metric-info">
            <div class="metric-icon"><i data-lucide="gauge"></i></div>
            <div>
              <div class="metric-label">Accel Magnitude</div>
              <div class="metric-value" id="valAccel">9.81 m/s²</div>
            </div>
          </div>
        </div>
      </div>

      <div class="card-title" style="margin-top: 10px;"><i data-lucide="wifi"></i> WiFi Origin Fix</div>
      
      <div class="metrics-list">
        <div class="metric-item">
          <div class="metric-info">
            <div class="metric-icon" style="color: var(--accent-gold); background: rgba(217,119,6,0.1);"><i data-lucide="map-pin"></i></div>
            <div>
              <div class="metric-label">Latitude</div>
              <div class="metric-value" id="valLat">21.125500</div>
            </div>
          </div>
        </div>

        <div class="metric-item">
          <div class="metric-info">
            <div class="metric-icon" style="color: var(--accent-gold); background: rgba(217,119,6,0.1);"><i data-lucide="map-pin"></i></div>
            <div>
              <div class="metric-label">Longitude</div>
              <div class="metric-value" id="valLng">79.052200</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Live Map Card -->
    <div class="glass-card map-card">
      <div class="map-overlay-header">
        <div class="map-pill"><i data-lucide="satellite" style="color: var(--accent-primary)"></i> ESP32 Hybrid WiFi + PDR Map Tracking</div>
        <div class="map-pill" id="accuracyPill"><i data-lucide="shield-check" style="color: var(--accent-emerald)"></i> Accuracy: ~12.0m</div>
      </div>
      <div id="map"></div>
    </div>

  </div>

  <!-- Team Members Info Modal -->
  <div class="modal-overlay" id="teamModal">
    <div class="modal-card">
      <div class="modal-header">
        <h2><i data-lucide="award" style="color: var(--accent-primary)"></i> Team Details</h2>
        <button class="close-btn" onclick="toggleModal(false)">&times;</button>
      </div>

      <p style="font-size: 13px; color: var(--text-muted); line-height: 1.5;">
        <strong>College:</strong> St. Vincent Pallotti College of Engineering and Technology<br>
        <strong>Event:</strong> VNIT Nagpur Ideathon 2026
      </p>

      <div class="team-member">
        <div class="member-avatar">SA</div>
        <div class="member-details">
          <h3>Samruddhi Atkare</h3>
          <p><i data-lucide="phone" style="width:14px;"></i> +91 8767123976</p>
        </div>
      </div>

      <div class="team-member">
        <div class="member-avatar">NT</div>
        <div class="member-details">
          <h3>Nandini Tayde</h3>
          <p><i data-lucide="phone" style="width:14px;"></i> +91 9146288982</p>
        </div>
      </div>

      <div class="team-member">
        <div class="member-avatar">VP</div>
        <div class="member-details">
          <h3>Vishwaja Pinjarkar</h3>
          <p><i data-lucide="phone" style="width:14px;"></i> +91 9373249454</p>
        </div>
      </div>
    </div>
  </div>

  <footer>
    Ideathon Project Prototype | St. Vincent Pallotti College of Engineering and Technology
  </footer>

  <script>
    lucide.createIcons();

    // Map Setup with Leaflet (Clean Modern Light Vector Maps)
    const initialLat = 21.1255;
    const initialLng = 79.0522;

    const map = L.map('map', { zoomControl: false }).setView([initialLat, initialLng], 18);

    // Light Vibrant Map Tile Layer (Positron by CartoDB / OSM)
    L.tileLayer('https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}{r}.png', {
      attribution: '&copy; OpenStreetMap &copy; CARTO',
      maxZoom: 20
    }).addTo(map);

    const nodeIcon = L.divIcon({ className: 'node-marker', iconSize: [20, 20], iconAnchor: [10, 10] });
    const targetMarker = L.marker([initialLat, initialLng], { icon: nodeIcon }).addTo(map);
    
    const pathLine = L.polyline([], {
      color: '#2563eb',
      weight: 5,
      opacity: 0.85,
      smoothFactor: 1
    }).addTo(map);

    function toggleModal(show) {
      document.getElementById('teamModal').style.display = show ? 'flex' : 'none';
    }

    function fetchTelemetry() {
      fetch('/api/telemetry')
        .then(res => res.json())
        .then(data => {
          document.getElementById('valSteps').textContent = data.step_count;
          document.getElementById('valHeading').textContent = `${data.heading.toFixed(1)}°`;
          document.getElementById('valOffset').textContent = `${data.offset_x.toFixed(1)}m / ${data.offset_y.toFixed(1)}m`;
          document.getElementById('valAccel').textContent = `${data.accel.toFixed(2)} m/s²`;
          document.getElementById('valLat').textContent = data.lat.toFixed(6);
          document.getElementById('valLng').textContent = data.lng.toFixed(6);

          document.getElementById('accuracyPill').innerHTML = `<i data-lucide="shield-check" style="color: var(--accent-emerald)"></i> Accuracy: ~${data.accuracy.toFixed(1)}m`;

          const newLatLng = [data.lat, data.lng];
          targetMarker.setLatLng(newLatLng);
          pathLine.addLatLng(newLatLng);

          map.panTo(newLatLng, { animate: true, duration: 1.0 });

          document.getElementById('statusDot').className = 'status-dot';
          document.getElementById('statusText').textContent = 'RECEIVING TELEMETRY';
          
          lucide.createIcons();
        })
        .catch(err => {
          document.getElementById('statusDot').className = 'status-dot offline';
          document.getElementById('statusText').textContent = 'OFFLINE / DISCONNECTED';
        });
    }

    setInterval(fetchTelemetry, 1500);
    fetchTelemetry();
  </script>
</body>
</html>
"""

class TelemetryHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/' or self.path == '/dashboard':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.end_headers()
            self.wfile.write(HTML_DASHBOARD.encode('utf-8'))
        elif self.path == '/api/telemetry':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(telemetry).encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == '/api/telemetry':
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            try:
                data = json.loads(body.decode('utf-8'))
                telemetry.update(data)
                telemetry["last_update"] = time.time()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"status": "SUCCESS"}).encode('utf-8'))
            except Exception as e:
                self.send_response(400)
                self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()

def run_server():
    with socketserver.TCPServer(("", PORT), TelemetryHandler) as httpd:
        print(f"ESP32 Telemetry Dashboard running at: http://localhost:{PORT}")
        print(f"ESP32 Telemetry Endpoint: http://{LOCAL_IP}:{PORT}/api/telemetry")
        httpd.serve_forever()

if __name__ == "__main__":
    run_server()

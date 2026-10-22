#include <WiFi.h>
#include <WebServer.h>

// =====================================================
// Local Wi-Fi Credentials (Station Mode)
// =====================================================
const char* ssid     = "drainx-controller";
const char* password = "12345678";

// =====================================================
// Web Server
// =====================================================
WebServer server(80);

// =====================================================
// Motor Pins - L298N
// =====================================================
#define IN1 14
#define IN2 27
#define IN3 26
#define IN4 25

#define ENA_PIN 33
#define ENB_PIN 32

// =====================================================
// Inversion Flags
// Toggle via Web UI or set here:
// If front wheels are Channel A (IN1/IN2) and rotate backwards,
// set invertChannelA = true.
// =====================================================
bool invertChannelA = false;
bool invertChannelB = false;

// =====================================================
// PWM Settings
// =====================================================
const int PWM_FREQ = 20000;   // 20 kHz
const int PWM_RES  = 8;       // 8-bit = 0-255

// =====================================================
// Motor Speeds
// NOTE: TT DC gear motors on L298N require 200-255 to start.
// Speeds below ~180 often stall due to L298N internal voltage drop (~2V).
// =====================================================
uint8_t manualSpeed = 230;
uint8_t autoSpeed   = 210;

// =====================================================
// Ultrasonic Sensor
// =====================================================
#define TRIG_PIN 4
#define ECHO_PIN 5

// =====================================================
// Control Flags
// =====================================================
bool forwardCmd = false;
bool backCmd    = false;
bool leftCmd    = false;
bool rightCmd   = false;
bool obstacleMode = false;

// =====================================================
// State Machine
// =====================================================
enum AutoState {
  AUTO_IDLE,
  AUTO_FORWARD,
  AUTO_BACKWARD,
  AUTO_TURN
};

AutoState autoState = AUTO_IDLE;
unsigned long autoStateStart = 0;
bool turnLeftNext = true;

const unsigned long BACK_TIME = 600;
const unsigned long TURN_TIME = 700;
const float OBSTACLE_DIST_CM = 20.0;

// =====================================================
// Web Page HTML
// =====================================================
const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>ESP32 Web Car</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      text-align: center;
      background: #121214;
      color: #eee;
      margin: 0;
      padding: 16px;
    }
    h1 { font-size: 22px; margin: 10px 0 6px; }
    .status-badge {
      display: inline-block;
      padding: 6px 14px;
      font-size: 14px;
      font-weight: 600;
      border-radius: 20px;
      background: #2a2a30;
      color: #00e676;
      margin-bottom: 12px;
    }
    .btn {
      display: inline-block;
      padding: 18px 26px;
      margin: 6px;
      font-size: 18px;
      font-weight: bold;
      border-radius: 12px;
      border: 1px solid #444;
      cursor: pointer;
      min-width: 105px;
      background: #23232a;
      color: #fff;
      user-select: none;
      -webkit-user-select: none;
      touch-action: manipulation;
      transition: all 0.1s ease;
    }
    .btn:active, .btn.active {
      transform: scale(0.95);
      background: #00c853;
      color: #000;
      border-color: #00e676;
    }
    .btn-stop {
      background: #d50000;
      border-color: #ff1744;
      color: #fff;
    }
    .btn-stop:active {
      background: #b71c1c;
      color: #fff;
    }
    .grid { margin: 15px auto; max-width: 360px; }
    .row { display: flex; justify-content: center; gap: 6px; margin: 6px 0; }
    .panel {
      background: #1c1c22;
      border: 1px solid #333;
      border-radius: 12px;
      padding: 14px;
      max-width: 380px;
      margin: 16px auto;
    }
    .panel h3 { margin: 0 0 10px 0; font-size: 16px; color: #aaa; text-transform: uppercase; letter-spacing: 1px; }
    .mode-btn-on { background: #1b5e20; border-color: #2e7d32; }
    .mode-btn-off { background: #37474f; border-color: #455a64; }
    .toggle-row { display: flex; justify-content: space-between; align-items: center; margin: 10px 0; }
    .toggle-row label { font-size: 14px; color: #ddd; }
    .invert-btn {
      padding: 10px 16px;
      font-size: 13px;
      border-radius: 8px;
      border: 1px solid #555;
      background: #2c2c36;
      color: #fff;
      cursor: pointer;
    }
    .invert-btn.inverted {
      background: #e65100;
      border-color: #ff9800;
      color: #fff;
      font-weight: bold;
    }
    .slider-row { margin: 12px 0 6px; }
    .slider-row input { width: 80%; }
    .mode-switch-btn {
      padding: 8px 14px;
      font-size: 13px;
      border-radius: 6px;
      border: 1px solid #555;
      background: #2a2a32;
      color: #ccc;
      cursor: pointer;
    }
    .mode-switch-btn.selected {
      background: #2979ff;
      color: #fff;
      border-color: #448aff;
      font-weight: bold;
    }
  </style>
</head>
<body>
  <h1>DrainX Motors Control</h1>
  <div id="statusBadge" class="status-badge">Status: STOPPED</div>

  <div style="margin: 10px 0;">
    <span style="font-size:13px; color:#aaa; margin-right:8px;">Control Mode:</span>
    <button id="btnHoldMode" class="mode-switch-btn selected" onclick="setDriveMode('hold')">Hold to Drive</button>
    <button id="btnContinuousMode" class="mode-switch-btn" onclick="setDriveMode('continuous')">Tap &amp; Stop</button>
  </div>

  <div class="grid">
    <div class="row">
      <button id="btn-f" class="btn" data-dir="f">Forward</button>
    </div>
    <div class="row">
      <button id="btn-l" class="btn" data-dir="l">Left</button>
      <button id="btn-s" class="btn btn-stop" onclick="stopCar()">STOP</button>
      <button id="btn-r" class="btn" data-dir="r">Right</button>
    </div>
    <div class="row">
      <button id="btn-b" class="btn" data-dir="b">Backward</button>
    </div>
  </div>

  <!-- Speed & Inversion Settings -->
  <div class="panel">
    <h3>Motor Tuning</h3>
    <div class="slider-row">
      <label>Speed: <span id="speedVal">230</span> / 255</label><br>
      <input type="range" id="speedSlider" min="150" max="255" value="230" onchange="sendSpeed(this.value)" oninput="document.getElementById('speedVal').innerText=this.value">
    </div>
    <div style="font-size: 12px; color: #888; margin-bottom: 12px;">Minimum 180-200 recommended to overcome L298N voltage drop.</div>

    <div class="toggle-row">
      <label>Front / Channel A (IN1/IN2):</label>
      <button id="btnInvA" class="invert-btn" onclick="toggleInvert('a')">Normal</button>
    </div>
    <div class="toggle-row">
      <label>Rear / Channel B (IN3/IN4):</label>
      <button id="btnInvB" class="invert-btn" onclick="toggleInvert('b')">Normal</button>
    </div>
  </div>

  <!-- Obstacle Mode -->
  <div class="panel">
    <h3>Obstacle Avoidance</h3>
    <div class="row">
      <button class="btn mode-btn-on" style="min-width:130px; font-size:15px;" onclick="setMode(1)">Mode ON</button>
      <button class="btn mode-btn-off" style="min-width:130px; font-size:15px;" onclick="setMode(0)">Mode OFF</button>
    </div>
  </div>

  <script>
    let driveMode = 'hold'; // 'hold' or 'continuous'
    let activeDir = '';

    function updateBadge(txt, color='#00e676') {
      const b = document.getElementById('statusBadge');
      b.innerText = txt;
      b.style.color = color;
    }

    function setDriveMode(mode) {
      driveMode = mode;
      document.getElementById('btnHoldMode').classList.toggle('selected', mode === 'hold');
      document.getElementById('btnContinuousMode').classList.toggle('selected', mode === 'continuous');
      stopCar();
    }

    function sendCmd(dir, state) {
      fetch('/cmd?dir=' + dir + '&state=' + state)
        .then(r => r.text())
        .then(() => {
          if (state === 0 && dir !== 's') {
            updateBadge('Status: STOPPED', '#00e676');
          } else if (state === 1) {
            const names = {'f':'FORWARD','b':'BACKWARD','l':'TURNING LEFT','r':'TURNING RIGHT','s':'STOPPED'};
            updateBadge('Status: ' + (names[dir] || dir), '#2979ff');
          }
        })
        .catch(err => {
          console.error(err);
          updateBadge('Error: ' + err.message, '#ff1744');
        });
    }

    function stopCar() {
      activeDir = '';
      sendCmd('s', 1);
      document.querySelectorAll('.btn').forEach(b => b.classList.remove('active'));
    }

    function sendSpeed(val) {
      fetch('/speed?val=' + val).catch(e => console.error(e));
    }

    function toggleInvert(ch) {
      fetch('/invert?ch=' + ch)
        .then(r => r.json())
        .then(data => {
          applyInvertUI(data);
        })
        .catch(e => console.error(e));
    }

    function applyInvertUI(data) {
      const btnA = document.getElementById('btnInvA');
      const btnB = document.getElementById('btnInvB');
      if (data.chA === 'INVERTED') {
        btnA.innerText = 'INVERTED';
        btnA.classList.add('inverted');
      } else {
        btnA.innerText = 'Normal';
        btnA.classList.remove('inverted');
      }
      if (data.chB === 'INVERTED') {
        btnB.innerText = 'INVERTED';
        btnB.classList.add('inverted');
      } else {
        btnB.innerText = 'Normal';
        btnB.classList.remove('inverted');
      }
    }

    function setMode(auto) {
      fetch('/mode?auto=' + auto)
        .then(() => {
          updateBadge(auto === 1 ? 'Status: AUTO OBSTACLE MODE' : 'Status: MANUAL MODE', auto === 1 ? '#ff9100' : '#00e676');
        })
        .catch(e => console.error(e));
    }

    // Attach button listeners supporting both Hold & Continuous modes
    ['f', 'b', 'l', 'r'].forEach(dir => {
      const btn = document.getElementById('btn-' + dir);
      if (!btn) return;

      const startAction = (e) => {
        if (e && e.cancelable) e.preventDefault();
        if (driveMode === 'hold') {
          btn.classList.add('active');
          sendCmd(dir, 1);
        } else {
          // Continuous / Tap mode
          if (activeDir === dir) {
            stopCar();
          } else {
            document.querySelectorAll('.btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            activeDir = dir;
            sendCmd(dir, 1);
          }
        }
      };

      const endAction = (e) => {
        if (driveMode === 'hold') {
          if (e && e.cancelable) e.preventDefault();
          btn.classList.remove('active');
          sendCmd(dir, 0);
        }
      };

      // Pointer / Mouse events
      btn.addEventListener('pointerdown', startAction);
      btn.addEventListener('pointerup', endAction);
      btn.addEventListener('pointerleave', endAction);
      btn.addEventListener('pointercancel', endAction);
    });

    // Initial status fetch on load
    fetch('/status')
      .then(r => r.json())
      .then(data => {
        if (data.speed) {
          document.getElementById('speedSlider').value = data.speed;
          document.getElementById('speedVal').innerText = data.speed;
        }
        applyInvertUI(data);
      })
      .catch(e => console.log('Init status:', e));
  </script>
</body>
</html>
)=====";

// =====================================================
// Motor Control Functions
// =====================================================
void setMotorSpeed(uint8_t leftSpeed, uint8_t rightSpeed) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(ENA_PIN, leftSpeed);
  ledcWrite(ENB_PIN, rightSpeed);
#else
  analogWrite(ENA_PIN, leftSpeed);
  analogWrite(ENB_PIN, rightSpeed);
#endif

  // Digital enable fallback: ensures L298N is energized even if PWM not configured
  if (leftSpeed > 0) digitalWrite(ENA_PIN, HIGH);
  else digitalWrite(ENA_PIN, LOW);

  if (rightSpeed > 0) digitalWrite(ENB_PIN, HIGH);
  else digitalWrite(ENB_PIN, LOW);
}

void setChannelA(bool forward) {
  bool dir = invertChannelA ? !forward : forward;
  if (dir) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
  }
}

void setChannelB(bool forward) {
  bool dir = invertChannelB ? !forward : forward;
  if (dir) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
  } else {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
  }
}

void driveStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  setMotorSpeed(0, 0);
}

void driveForward(uint8_t speed) {
  setChannelA(true);
  setChannelB(true);
  setMotorSpeed(speed, speed);
}

void driveBackward(uint8_t speed) {
  setChannelA(false);
  setChannelB(false);
  setMotorSpeed(speed, speed);
}

void driveLeft(uint8_t speed) {
  setChannelA(false);
  setChannelB(true);
  setMotorSpeed(speed, speed);
}

void driveRight(uint8_t speed) {
  setChannelA(true);
  setChannelB(false);
  setMotorSpeed(speed, speed);
}

// =====================================================
// Ultrasonic Distance Sensor
// =====================================================
float getDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (duration == 0) return -1.0;

  return (duration * 0.0343) / 2.0;
}

// =====================================================
// Drive Mode Logic
// =====================================================
void handleManualMovement() {
  if (forwardCmd)      driveForward(manualSpeed);
  else if (backCmd)    driveBackward(manualSpeed);
  else if (leftCmd)    driveLeft(manualSpeed);
  else if (rightCmd)   driveRight(manualSpeed);
  else                 driveStop();
}

void handleAutoObstacle() {
  unsigned long now = millis();

  switch (autoState) {
    case AUTO_IDLE:
      driveStop();
      autoState = AUTO_FORWARD;
      autoStateStart = now;
      break;

    case AUTO_FORWARD: {
      float distance = getDistanceCm();
      if (distance > 0 && distance < OBSTACLE_DIST_CM) {
        Serial.printf("[OBSTACLE] Detected @ %.1f cm -> Backing up\n", distance);
        driveStop();
        autoState = AUTO_BACKWARD;
        autoStateStart = now;
      } else {
        driveForward(autoSpeed);
      }
      break;
    }

    case AUTO_BACKWARD:
      if (now - autoStateStart >= BACK_TIME) {
        driveStop();
        autoState = AUTO_TURN;
        autoStateStart = now;
      } else {
        driveBackward(autoSpeed);
      }
      break;

    case AUTO_TURN:
      if (turnLeftNext) driveLeft(autoSpeed);
      else              driveRight(autoSpeed);

      if (now - autoStateStart >= TURN_TIME) {
        driveStop();
        turnLeftNext = !turnLeftNext;
        autoState = AUTO_FORWARD;
        autoStateStart = now;
      }
      break;
  }
}

// =====================================================
// Web Handlers
// =====================================================
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleCmd() {
  if (!server.hasArg("dir") || !server.hasArg("state")) {
    server.send(400, "text/plain", "Missing params");
    return;
  }

  String dir = server.arg("dir");
  int state = server.arg("state").toInt();
  Serial.printf("[CMD] dir: %s, state: %d (ObstacleMode: %d)\n", dir.c_str(), state, obstacleMode);

  if (!obstacleMode) {
    bool pressed = (state != 0);

    if (dir == "f") {
      forwardCmd = pressed;
      if (pressed) { backCmd = false; leftCmd = false; rightCmd = false; Serial.printf("[DRIVE] FORWARD @ Speed %d\n", manualSpeed); }
      else { driveStop(); Serial.println("[DRIVE] STOP"); }
    } else if (dir == "b") {
      backCmd = pressed;
      if (pressed) { forwardCmd = false; leftCmd = false; rightCmd = false; Serial.printf("[DRIVE] BACKWARD @ Speed %d\n", manualSpeed); }
      else { driveStop(); Serial.println("[DRIVE] STOP"); }
    } else if (dir == "l") {
      leftCmd = pressed;
      if (pressed) { forwardCmd = false; backCmd = false; rightCmd = false; Serial.printf("[DRIVE] LEFT @ Speed %d\n", manualSpeed); }
      else { driveStop(); Serial.println("[DRIVE] STOP"); }
    } else if (dir == "r") {
      rightCmd = pressed;
      if (pressed) { forwardCmd = false; backCmd = false; leftCmd = false; Serial.printf("[DRIVE] RIGHT @ Speed %d\n", manualSpeed); }
      else { driveStop(); Serial.println("[DRIVE] STOP"); }
    } else if (dir == "s") {
      forwardCmd = false; backCmd = false; leftCmd = false; rightCmd = false;
      driveStop();
      Serial.println("[DRIVE] STOP");
    }
  }

  server.send(200, "text/plain", "OK");
}

void handleSpeed() {
  if (server.hasArg("val")) {
    int s = server.arg("val").toInt();
    if (s >= 100 && s <= 255) {
      manualSpeed = (uint8_t)s;
      Serial.printf("[SPEED] Updated manual speed: %d\n", manualSpeed);
    }
  }
  server.send(200, "text/plain", String(manualSpeed));
}

void handleInvert() {
  if (server.hasArg("ch")) {
    String ch = server.arg("ch");
    if (ch == "a") {
      invertChannelA = !invertChannelA;
      Serial.printf("[INVERT] Channel A is now: %s\n", invertChannelA ? "INVERTED" : "NORMAL");
    } else if (ch == "b") {
      invertChannelB = !invertChannelB;
      Serial.printf("[INVERT] Channel B is now: %s\n", invertChannelB ? "INVERTED" : "NORMAL");
    }
  }
  String status = "{\"chA\":\"" + String(invertChannelA ? "INVERTED" : "NORMAL") + "\",\"chB\":\"" + String(invertChannelB ? "INVERTED" : "NORMAL") + "\"}";
  server.send(200, "application/json", status);
}

void handleStatus() {
  String json = "{";
  json += "\"speed\":" + String(manualSpeed) + ",";
  json += "\"chA\":\"" + String(invertChannelA ? "INVERTED" : "NORMAL") + "\",";
  json += "\"chB\":\"" + String(invertChannelB ? "INVERTED" : "NORMAL") + "\",";
  json += "\"obstacle\":" + String(obstacleMode ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleMode() {
  if (!server.hasArg("auto")) {
    server.send(400, "text/plain", "Missing auto param");
    return;
  }

  int autoVal = server.arg("auto").toInt();

  if (autoVal == 1) {
    obstacleMode = true;
    forwardCmd = false; backCmd = false; leftCmd = false; rightCmd = false;
    autoState = AUTO_FORWARD;
    autoStateStart = millis();
    Serial.println("[MODE] Obstacle Avoidance ON");
  } else {
    obstacleMode = false;
    autoState = AUTO_IDLE;
    driveStop();
    Serial.println("[MODE] Obstacle Avoidance OFF");
  }

  server.send(200, "text/plain", "Mode set");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  // Pin Configuration
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA_PIN, OUTPUT);
  pinMode(ENB_PIN, OUTPUT);

  // PWM Configuration
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(ENA_PIN, PWM_FREQ, PWM_RES);
  ledcAttach(ENB_PIN, PWM_FREQ, PWM_RES);
#endif

  driveStop();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // =================================================
  // Wi-Fi Station Connection
  // =================================================
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("\nConnecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n==============================");
  Serial.println("CONNECTED TO WI-FI");
  Serial.println("==============================");
  Serial.print("Access web control at: http://");
  Serial.println(WiFi.localIP());
  Serial.println("==============================");

  // Server Routing
  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/speed", handleSpeed);
  server.on("/invert", handleInvert);
  server.on("/status", handleStatus);
  server.on("/mode", handleMode);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  server.handleClient();

  if (obstacleMode) {
    handleAutoObstacle();
  } else {
    handleManualMovement();
  }

  delay(5);
}

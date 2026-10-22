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
// PWM Settings
// =====================================================
const int PWM_FREQ = 20000;   // 20 kHz
const int PWM_RES  = 8;       // 8-bit = 0-255

// =====================================================
// Motor Speeds
// =====================================================
uint8_t manualSpeed = 230;
uint8_t autoSpeed   = 200;

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
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      background: #111;
      color: #eee;
      margin: 0;
      padding: 10px;
    }
    h1 { margin-top: 10px; }
    h2 { margin-top: 30px; }
    p { color: #bbb; }
    .btn {
      display: inline-block;
      padding: 18px 26px;
      margin: 8px;
      font-size: 18px;
      border-radius: 10px;
      border: none;
      cursor: pointer;
      min-width: 110px;
      background: #333;
      color: #fff;
      user-select: none;
      -webkit-user-select: none;
      touch-action: manipulation;
    }
    .btn:active {
      transform: scale(0.97);
      background: #555;
    }
    .grid { margin-top: 25px; }
    .row { margin: 6px 0; }
    .mode-btn-on { background: #1b5e20; }
    .mode-btn-off { background: #b71c1c; }
  </style>
</head>
<body>
  <h1>ESP32 Web Controlled Car</h1>
  <p>Hold the direction buttons to move. Toggle Obstacle Mode for automatic driving.</p>

  <div class="grid">
    <div class="row">
      <button class="btn" onmousedown="sendCmd('f',1)" onmouseup="sendCmd('f',0)" onmouseleave="sendCmd('f',0)" ontouchstart="sendCmd('f',1)" ontouchend="sendCmd('f',0)">Forward</button>
    </div>
    <div class="row">
      <button class="btn" onmousedown="sendCmd('l',1)" onmouseup="sendCmd('l',0)" onmouseleave="sendCmd('l',0)" ontouchstart="sendCmd('l',1)" ontouchend="sendCmd('l',0)">Left</button>
      <button class="btn" onclick="stopCar()">Stop</button>
      <button class="btn" onmousedown="sendCmd('r',1)" onmouseup="sendCmd('r',0)" onmouseleave="sendCmd('r',0)" ontouchstart="sendCmd('r',1)" ontouchend="sendCmd('r',0)">Right</button>
    </div>
    <div class="row">
      <button class="btn" onmousedown="sendCmd('b',1)" onmouseup="sendCmd('b',0)" onmouseleave="sendCmd('b',0)" ontouchstart="sendCmd('b',1)" ontouchend="sendCmd('b',0)">Backward</button>
    </div>
  </div>

  <h2>Obstacle Avoiding Mode</h2>
  <p>When ON, the car moves forward and automatically avoids obstacles.</p>
  <div class="row">
    <button class="btn mode-btn-on" onclick="setMode(1)">Mode ON</button>
    <button class="btn mode-btn-off" onclick="setMode(0)">Mode OFF</button>
  </div>

  <script>
    function sendCmd(dir, state) {
      fetch('/cmd?dir=' + dir + '&state=' + state).catch(err => console.log(err));
    }
    function stopCar() {
      fetch('/cmd?dir=s&state=1').catch(err => console.log(err));
    }
    function setMode(auto) {
      fetch('/mode?auto=' + auto).catch(err => console.log(err));
    }
  </script>
</body>
</html>
)=====";

// =====================================================
// Motor Control Functions
// =====================================================
void setMotorSpeed(uint8_t leftSpeed, uint8_t rightSpeed) {
  ledcWrite(ENA_PIN, leftSpeed);
  ledcWrite(ENB_PIN, rightSpeed);
}

void driveStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  setMotorSpeed(0, 0);
}

void driveForward(uint8_t speed) {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  setMotorSpeed(speed, speed);
}

void driveBackward(uint8_t speed) {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  setMotorSpeed(speed, speed);
}

void driveLeft(uint8_t speed) {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  setMotorSpeed(speed, speed);
}

void driveRight(uint8_t speed) {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
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

  if (!obstacleMode) {
    bool pressed = (state != 0);

    if (dir == "f") {
      forwardCmd = pressed;
      if (pressed) { backCmd = false; leftCmd = false; rightCmd = false; }
    } else if (dir == "b") {
      backCmd = pressed;
      if (pressed) { forwardCmd = false; leftCmd = false; rightCmd = false; }
    } else if (dir == "l") {
      leftCmd = pressed;
      if (pressed) { forwardCmd = false; backCmd = false; rightCmd = false; }
    } else if (dir == "r") {
      rightCmd = pressed;
      if (pressed) { forwardCmd = false; backCmd = false; leftCmd = false; }
    } else if (dir == "s") {
      forwardCmd = false; backCmd = false; leftCmd = false; rightCmd = false;
      driveStop();
    }
  }

  server.send(200, "text/plain", "OK");
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
  } else {
    obstacleMode = false;
    autoState = AUTO_IDLE;
    driveStop();
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

  // ESP32 Arduino Core 3.x LEDC Configuration
  ledcAttach(ENA_PIN, PWM_FREQ, PWM_RES);
  ledcAttach(ENB_PIN, PWM_FREQ, PWM_RES);

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
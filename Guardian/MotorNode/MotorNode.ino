/*
 * DrainX Guardian — MotorNode
 *
 * Role: "motor" in the Guardian architecture.
 *
 * Auto-discovers into the dashboard via UDP beacon (port 5005).
 * Accepts movement commands via HTTP from the Python server:
 *
 *   GET /move?dir=f|b|l|r|s[&dur=<ms>]
 *       f = forward, b = backward, l = left, r = right, s = stop
 *       dur = optional duration in ms (default: 0 = hold until next command)
 *
 *   GET /status   -> JSON with speed, direction, invertions, etc.
 *   GET /info     -> JSON node identity (matches Guardian convention)
 *   GET /speed?val=150-255
 *
 * Pinout (L298N driver, ESP32 WROOM):
 *   IN1 -> GPIO 14   (Channel A — Left Side)
 *   IN2 -> GPIO 27
 *   IN3 -> GPIO 26   (Channel B — Right Side)
 *   IN4 -> GPIO 18   [remapped from 25, which was found faulty]
 *   ENA -> GPIO 33
 *   ENB -> GPIO 32
 *
 * Hardware note: Two motors per channel are spliced in parallel.
 * If a motor on a side spins in the wrong direction, swap its wire
 * pair physically (the invertChannelA / invertChannelB flags can
 * flip a whole side, but cannot fix individual motors on the same
 * channel).
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>

// =====================================================
// Network & Discovery
// =====================================================
const char* ssid     = "drainx-controller";
const char* password = "12345678";
const int   DISCOVERY_PORT = 5005;

WebServer server(80);
WiFiUDP   udp;
unsigned long lastBeaconTime = 0;

void broadcastBeacon() {
  if (WiFi.status() != WL_CONNECTED) return;
  IPAddress broadcastIp(255, 255, 255, 255);
  String mac = WiFi.macAddress();
  String ip  = WiFi.localIP().toString();
  String msg = "{\"device\":\"drainx-guardian\",\"role\":\"motor\","
               "\"mac\":\"" + mac + "\","
               "\"ip\":\""  + ip  + "\","
               "\"port\":80}";
  udp.beginPacket(broadcastIp, DISCOVERY_PORT);
  udp.print(msg);
  udp.endPacket();
}

// =====================================================
// Motor Pins — L298N
// =====================================================
#define IN1     14   // Channel A — Left Side
#define IN2     27
#define IN3     26   // Channel B — Right Side
#define IN4     18   // Remapped: GPIO 25 was found non-functional
#define ENA_PIN 33
#define ENB_PIN 32

// =====================================================
// PWM Settings
// =====================================================
const int PWM_FREQ = 20000;  // 20 kHz (above audible range)
const int PWM_RES  = 8;      // 8-bit: 0-255

// =====================================================
// Runtime State
// =====================================================
uint8_t motorSpeed    = 150;   // Default drive speed (150/255)
bool invertChannelA   = false; // Flip left-side direction in software
bool invertChannelB   = false; // Flip right-side direction in software
String currentDir     = "s";   // Last commanded direction

bool    timedMove     = false;
unsigned long moveEndTime = 0;

// =====================================================
// Low-Level Motor Helpers
// =====================================================
void applyPWM(uint8_t leftSpeed, uint8_t rightSpeed) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(ENA_PIN, leftSpeed);
  ledcWrite(ENB_PIN, rightSpeed);
#else
  analogWrite(ENA_PIN, leftSpeed);
  analogWrite(ENB_PIN, rightSpeed);
#endif
}

void setChannelA(bool forward) {
  bool d = invertChannelA ? !forward : forward;
  digitalWrite(IN1, d ? HIGH : LOW);
  digitalWrite(IN2, d ? LOW  : HIGH);
}

void setChannelB(bool forward) {
  bool d = invertChannelB ? !forward : forward;
  digitalWrite(IN3, d ? HIGH : LOW);
  digitalWrite(IN4, d ? LOW  : HIGH);
}

// =====================================================
// Drive Commands
// =====================================================
void driveStop() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  applyPWM(0, 0);
  currentDir = "s";
  timedMove  = false;
  Serial.println("[MOTOR] STOP");
}

void driveForward(uint8_t spd) {
  setChannelA(true); setChannelB(true);
  applyPWM(spd, spd);
  currentDir = "f";
  Serial.printf("[MOTOR] FORWARD @ %d\n", spd);
}

void driveBackward(uint8_t spd) {
  setChannelA(false); setChannelB(false);
  applyPWM(spd, spd);
  currentDir = "b";
  Serial.printf("[MOTOR] BACKWARD @ %d\n", spd);
}

void driveLeft(uint8_t spd) {
  setChannelA(false); setChannelB(true);
  applyPWM(spd, spd);
  currentDir = "l";
  Serial.printf("[MOTOR] LEFT @ %d\n", spd);
}

void driveRight(uint8_t spd) {
  setChannelA(true); setChannelB(false);
  applyPWM(spd, spd);
  currentDir = "r";
  Serial.printf("[MOTOR] RIGHT @ %d\n", spd);
}

void executeDir(String dir, uint8_t spd) {
  if      (dir == "f") driveForward(spd);
  else if (dir == "b") driveBackward(spd);
  else if (dir == "l") driveLeft(spd);
  else if (dir == "r") driveRight(spd);
  else                 driveStop();
}

// =====================================================
// HTTP Handlers
// =====================================================

// GET /info  — standard Guardian node identity
void handleInfo() {
  String json = "{\"device\":\"drainx-guardian\",\"role\":\"motor\","
                "\"mac\":\""  + WiFi.macAddress()        + "\","
                "\"ip\":\""   + WiFi.localIP().toString() + "\","
                "\"port\":80}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// GET /status  — runtime state
void handleStatus() {
  String json = "{";
  json += "\"dir\":\""    + currentDir + "\",";
  json += "\"speed\":"    + String(motorSpeed)      + ",";
  json += "\"invertA\":"  + String(invertChannelA ? "true" : "false") + ",";
  json += "\"invertB\":"  + String(invertChannelB ? "true" : "false") + ",";
  json += "\"timedMove\":" + String(timedMove ? "true" : "false");
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// GET /move?dir=f|b|l|r|s[&dur=ms][&speed=150-255]
// Called by the Python server to move the robot.
void handleMove() {
  String dir = server.hasArg("dir") ? server.arg("dir") : "s";
  unsigned long dur = server.hasArg("dur") ? (unsigned long)server.arg("dur").toInt() : 0;
  if (server.hasArg("speed")) {
    int s = server.arg("speed").toInt();
    if (s >= 100 && s <= 255) motorSpeed = (uint8_t)s;
  }

  executeDir(dir, motorSpeed);

  if (dur > 0) {
    timedMove   = true;
    moveEndTime = millis() + dur;
  } else {
    timedMove = false;
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json",
    "{\"ok\":true,\"dir\":\"" + dir + "\",\"dur\":" + String(dur) + "}");
}

// GET /speed?val=100-255
void handleSpeed() {
  if (server.hasArg("val")) {
    int s = server.arg("val").toInt();
    if (s >= 100 && s <= 255) {
      motorSpeed = (uint8_t)s;
      if (currentDir != "s") {
        applyPWM(motorSpeed, motorSpeed);
      }
      Serial.printf("[MOTOR] Speed updated to %d\n", motorSpeed);
    }
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", String(motorSpeed));
}

// GET /invert?ch=a|b
void handleInvert() {
  if (server.hasArg("ch")) {
    String ch = server.arg("ch");
    if (ch == "a") { invertChannelA = !invertChannelA; Serial.printf("[MOTOR] Channel A invert: %s\n", invertChannelA ? "ON" : "OFF"); }
    if (ch == "b") { invertChannelB = !invertChannelB; Serial.printf("[MOTOR] Channel B invert: %s\n", invertChannelB ? "ON" : "OFF"); }
  }
  String json = "{\"invertA\":" + String(invertChannelA ? "true" : "false") +
                ",\"invertB\":" + String(invertChannelB ? "true" : "false") + "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  // Configure motor control pins
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA_PIN, OUTPUT); pinMode(ENB_PIN, OUTPUT);

  // Attach PWM channels (ESP32 Arduino Core 3.x style)
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(ENA_PIN, PWM_FREQ, PWM_RES);
  ledcAttach(ENB_PIN, PWM_FREQ, PWM_RES);
#endif

  driveStop();

  // Connect to Wi-Fi (disable modem sleep for low-latency motor commands)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n==============================");
  Serial.println("CONNECTED — MotorNode Online");
  Serial.println("==============================");
  Serial.print("IP:  "); Serial.println(WiFi.localIP());
  Serial.print("MAC: "); Serial.println(WiFi.macAddress());
  Serial.println("==============================");

  // Start UDP for auto-discovery
  udp.begin(DISCOVERY_PORT);

  // Register HTTP endpoints
  server.on("/info",     handleInfo);
  server.on("/api/info", handleInfo);
  server.on("/status",   handleStatus);
  server.on("/move",     handleMove);
  server.on("/api/move", handleMove);
  server.on("/speed",    handleSpeed);
  server.on("/invert",   handleInvert);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started on port 80");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  server.handleClient();

  // Auto-stop after timed move
  if (timedMove && millis() >= moveEndTime) {
    driveStop();
  }

  // Broadcast discovery beacon every 2 seconds
  if (millis() - lastBeaconTime > 2000) {
    lastBeaconTime = millis();
    broadcastBeacon();
  }

  delay(5);
}

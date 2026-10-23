/*
 * DrainX Guardian - ArmNode (Arduino Uno)
 * 
 * Controls the robotic arm servo used for collecting debris from drains.
 * Hardware Connection:
 *   - Arduino Uno board
 *   - Servo Signal connected to Pin 9 (PWM)
 *   - Servo VCC connected to external 5V power supply
 *   - Servo GND connected to Common Ground (Arduino GND + Power Supply GND)
 * 
 * Communication:
 *   - USB Serial at 115200 baud
 *   - Broadcasts JSON heartbeat every 2 seconds:
 *       {"device":"drainx-guardian","role":"arm","status":"online","angle":90}
 *   - Accepts commands:
 *       "A:90\n" or "A90\n" or "90\n" -> Sets servo to angle (0 - 180 degrees)
 *       "PING\n" or "STATUS\n"        -> Replies immediately with current status
 */

#include <Servo.h>

const int SERVO_PIN = 9;
const long SERIAL_BAUD = 115200;
const unsigned long HEARTBEAT_INTERVAL = 2000; // 2 seconds

Servo armServo;

int currentAngle = 90;
unsigned long lastHeartbeat = 0;
String inputBuffer = "";

void sendStatus() {
  Serial.print(F("{\"device\":\"drainx-guardian\",\"role\":\"arm\",\"status\":\"online\",\"angle\":"));
  Serial.print(currentAngle);
  Serial.println(F("}"));
}

void setArmAngle(int angle) {
  angle = constrain(angle, 0, 180);
  currentAngle = angle;
  armServo.write(currentAngle);
  sendStatus();
}

void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd == "PING" || cmd == "STATUS") {
    sendStatus();
    return;
  }

  // Handle "A:90" or "A90"
  if (cmd.startsWith("A:") || cmd.startsWith("A")) {
    int colonIdx = cmd.indexOf(':');
    String valStr = (colonIdx >= 0) ? cmd.substring(colonIdx + 1) : cmd.substring(1);
    valStr.trim();
    int angle = valStr.toInt();
    setArmAngle(angle);
  }
  // Handle JSON {"angle": 90}
  else if (cmd.indexOf("ANGLE") >= 0) {
    int colonIdx = cmd.indexOf(':');
    int braceIdx = cmd.indexOf('}');
    if (colonIdx >= 0) {
      String valStr = (braceIdx > colonIdx) ? cmd.substring(colonIdx + 1, braceIdx) : cmd.substring(colonIdx + 1);
      valStr.trim();
      setArmAngle(valStr.toInt());
    }
  }
  // Handle raw numeric angle: "90"
  else {
    int angle = cmd.toInt();
    if (angle > 0 || cmd == "0") {
      setArmAngle(angle);
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  inputBuffer.reserve(64);

  // Attach servo to Pin 9 and initialize to 90 degrees
  armServo.attach(SERVO_PIN);
  armServo.write(currentAngle);

  // Brief delay for power stabilization
  delay(250);
  sendStatus();
}

void loop() {
  // Read incoming Serial characters
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
        inputBuffer = "";
      }
    } else {
      if (inputBuffer.length() < 60) {
        inputBuffer += c;
      }
    }
  }

  // Periodic heartbeat beacon
  unsigned long now = millis();
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = now;
    sendStatus();
  }
}

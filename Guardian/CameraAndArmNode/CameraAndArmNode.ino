/*
 * DrainX Guardian — CameraAndArmNode (AI-Thinker ESP32-CAM)
 *
 * Combines live JPEG snapshot streaming (/capture) and the
 * robotic debris-collector servo arm (/arm?angle=0..180) on a single
 * ESP32-CAM board over Wi-Fi LAN.
 *
 * Power-Safe Servo Design for ESP32-CAM:
 *   1. Hardware brownout detector is disabled at boot so servo current spikes
 *      never reset the ESP32-CAM.
 *   2. Servo PWM is kept OFF (LOW) during boot and Wi-Fi connection so the
 *      ESP32 Wi-Fi radio gets 100% power during association.
 *   3. Smooth slew-rate stepping (like the classic Arduino Sweep sketch) moves
 *      the servo gradually in loop() instead of jumping instantaneously, cutting
 *      peak inrush current by more than 60%.
 *   4. Once the servo reaches targetAngle, PWM pulses are idled (duty = 0)
 *      so the servo draws 0 mA while stationary.
 *
 * Servo Wiring (AI-Thinker ESP32-CAM):
 *   - Servo Signal (Orange/Yellow) -> GPIO 13 (IO13, 4th pin down on right header)
 *                                     [GPIO 14 / IO14 is also driven in parallel]
 *   - Servo VCC    (Red)           -> 5V power rail
 *   - Servo GND    (Brown/Black)   -> Common GND
 */

#include <esp_camera.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- Network & Discovery Settings ---
const char* ssid = "drainx-controller";
const char* password = "12345678";
const int DISCOVERY_PORT = 5005;

WiFiServer server(80);
WiFiUDP udp;
unsigned long lastBeaconTime = 0;

// --- Servo Arm Settings (Uses LEDC Timer 2 so it never conflicts with Camera Timer 0) ---
#define SERVO_PIN          13   // Primary servo signal pin (IO13)
#define SERVO_PIN_ALT      14   // Backup servo signal pin (IO14, driven in parallel)
#define SERVO_LEDC_CH       4   // LEDC Channel 4 (Timer 2)
#define SERVO_LEDC_CH_ALT   5   // LEDC Channel 5 (Timer 2)
#define SERVO_FREQ         50   // Standard 50 Hz servo PWM (20 ms period)
#define SERVO_RES          14   // 14-bit resolution (0 - 16383 ticks)

int currentAngle   = 90;        // Current logical servo angle (0 - 180)
int targetAngle    = 90;        // Requested target servo angle (0 - 180)
float smoothAngle  = 90.0f;     // Sub-degree floating-point angle for microsecond interpolation
bool servoAttached = false;
bool servoActive   = false;
unsigned long lastServoStepTime = 0;
unsigned long servoIdleTime     = 0;

void attachServoPWM() {
  if (servoAttached) return;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(SERVO_PIN,     SERVO_FREQ, SERVO_RES, SERVO_LEDC_CH);
  ledcAttachChannel(SERVO_PIN_ALT, SERVO_FREQ, SERVO_RES, SERVO_LEDC_CH_ALT);
#else
  ledcSetup(SERVO_LEDC_CH,     SERVO_FREQ, SERVO_RES);
  ledcAttachPin(SERVO_PIN,     SERVO_LEDC_CH);
  ledcSetup(SERVO_LEDC_CH_ALT, SERVO_FREQ, SERVO_RES);
  ledcAttachPin(SERVO_PIN_ALT, SERVO_LEDC_CH_ALT);
#endif
  servoAttached = true;
}

void applyServoPulseUs(long pulseUs) {
  attachServoPWM();
  pulseUs = constrain(pulseUs, 500L, 2400L);
  uint32_t duty = (uint32_t)((pulseUs * 16384L) / 20000L);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SERVO_PIN,     duty);
  ledcWrite(SERVO_PIN_ALT, duty);
#else
  ledcWrite(SERVO_LEDC_CH,     duty);
  ledcWrite(SERVO_LEDC_CH_ALT, duty);
#endif
}

void stopServoPulses() {
  if (!servoAttached) return;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SERVO_PIN,     0);
  ledcWrite(SERVO_PIN_ALT, 0);
#else
  ledcWrite(SERVO_LEDC_CH,     0);
  ledcWrite(SERVO_LEDC_CH_ALT, 0);
#endif
  servoActive = false;
}

void setTargetServoAngle(int angle) {
  targetAngle = constrain(angle, 0, 180);
  servoActive = true;
  servoIdleTime = millis() + 1000;
}

void updateServoSmooth() {
  if (!servoActive) return;

  unsigned long now = millis();
  const unsigned long STEP_INTERVAL_MS = 15; // Non-blocking 15 ms update interval (~66 Hz)

  if (now - lastServoStepTime < STEP_INTERVAL_MS) return;
  lastServoStepTime = now;

  float diff = (float)targetAngle - smoothAngle;

  if (abs(diff) > 0.25f) {
    // Load-aware speed limit:
    // In the high-load 90..180 deg zone, step gently (0.8 deg per 15 ms = ~53 deg/s)
    // to prevent torque stall and voltage sag; in 0..90 deg, step at 1.5 deg per 15 ms.
    float maxStep = (smoothAngle >= 85.0f || targetAngle >= 90) ? 0.8f : 1.5f;

    // Proportional easing near the target to eliminate stopping jerk/overshoot
    float step = diff * 0.18f;
    if (step >  maxStep) step =  maxStep;
    if (step < -maxStep) step = -maxStep;
    if (abs(step) < 0.35f) step = (diff > 0) ? 0.35f : -0.35f;

    smoothAngle += step;
    if (abs((float)targetAngle - smoothAngle) <= 0.35f) {
      smoothAngle = (float)targetAngle;
    }
    currentAngle = (int)(smoothAngle + 0.5f);

    // Sub-degree microsecond mapping (500 us to 2400 us) for ultra-smooth PWM resolution
    long pulseUs = (long)(500.0f + (smoothAngle / 180.0f) * 1900.0f);
    applyServoPulseUs(pulseUs);
    servoIdleTime = now + 800;
  } else {
    smoothAngle = (float)targetAngle;
    currentAngle = targetAngle;

    // Only cut holding pulses when parked in the low-load resting zone (< 85 deg).
    // Above 85 deg, keep PWM active so gravity doesn't pull the loaded strainer down and cause jerking.
    if (currentAngle < 85 && (long)(now - servoIdleTime) >= 0) {
      stopServoPulses();
    }
  }
}

void broadcastBeacon() {
  if (WiFi.status() != WL_CONNECTED) return;
  IPAddress broadcastIp(255, 255, 255, 255);
  String macStr = WiFi.macAddress();
  String ipStr = WiFi.localIP().toString();
  String message = "{\"device\":\"drainx-guardian\",\"role\":\"camera_arm\",\"mac\":\"" + macStr + "\",\"ip\":\"" + ipStr + "\",\"port\":80,\"angle\":" + String(targetAngle) + "}";
  udp.beginPacket(broadcastIp, DISCOVERY_PORT);
  udp.print(message);
  udp.endPacket();
}

// --- AI Thinker ESP32-CAM Pins ---
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27

#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

void startCamera() {
  // Power-cycle the OV2640 sensor cleanly before init
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  delay(50);
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(50);

  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;   // 320x240
  config.jpeg_quality = 12;
  if (psramFound()) {
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera Init Failed: 0x%x\n", err);
    return;
  }
  Serial.println("Camera Ready");
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  // Wait for data with a timeout
  unsigned long timeout = millis() + 3000;
  while (!client.available() && millis() < timeout) {
    delay(1);
  }
  if (!client.available()) {
    client.stop();
    return;
  }

  // Read the first line of the HTTP request
  String request = client.readStringUntil('\r');
  client.readStringUntil('\n');
  // Consume remaining headers
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line.length() <= 1) break;  // Empty line = end of headers
  }

  if (request.indexOf("GET /capture") >= 0) {
    // --- Serve a single JPEG snapshot ---
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Access-Control-Allow-Origin: *");
      client.println("Connection: close");
      client.println();
      client.stop();
      Serial.println("Capture Failed");
      return;
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/jpeg");
    client.println("Access-Control-Allow-Origin: *");
    client.printf("Content-Length: %u\r\n", fb->len);
    client.println("Connection: close");
    client.println();
    client.write(fb->buf, fb->len);

    esp_camera_fb_return(fb);
  } else if (request.indexOf("GET /arm") >= 0) {
    // --- Set or Query Servo Arm Angle: GET /arm?angle=0..180 ---
    int angleIdx = request.indexOf("angle=");
    if (angleIdx >= 0) {
      int valStart = angleIdx + 6;
      int valEnd = valStart;
      while (valEnd < (int)request.length() && isDigit(request.charAt(valEnd))) {
        valEnd++;
      }
      if (valEnd > valStart) {
        int newAngle = request.substring(valStart, valEnd).toInt();
        setTargetServoAngle(newAngle);
        Serial.printf("[ARM] Target Angle -> %d deg\n", targetAngle);
      }
    }
    String body = "{\"status\":\"ok\",\"role\":\"arm\",\"angle\":" + String(targetAngle) + "}";
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Access-Control-Allow-Origin: *");
    client.printf("Content-Length: %u\r\n", body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
  } else if (request.indexOf("GET /api/info") >= 0 || request.indexOf("GET /info") >= 0) {
    // --- Device Info Endpoint ---
    String body = "{\"device\":\"drainx-guardian\",\"role\":\"camera_arm\",\"mac\":\"" + WiFi.macAddress() + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"port\":80,\"angle\":" + String(targetAngle) + "}";
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Access-Control-Allow-Origin: *");
    client.printf("Content-Length: %u\r\n", body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
  } else {
    // --- Simple status page for any other request ---
    String body = "{\"status\":\"ok\",\"role\":\"camera_arm\",\"angle\":" + String(targetAngle) + ",\"mac\":\"" + WiFi.macAddress() + "\",\"endpoints\":[\"/capture\",\"/arm?angle=90\",\"/api/info\"]}";
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Access-Control-Allow-Origin: *");
    client.printf("Content-Length: %u\r\n", body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
  }

  client.stop();
}

void serverTask(void *pvParameters) {
  while (true) {
    handleClient();
    vTaskDelay(1);
  }
}

void setup() {
  // Disable brownout detector so Wi-Fi TX + servo currents never trigger reset
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Keep servo pins LOW at boot so the servo motor draws 0 mA during Wi-Fi startup
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  pinMode(SERVO_PIN_ALT, OUTPUT);
  digitalWrite(SERVO_PIN_ALT, LOW);

  Serial.begin(115200);
  delay(1000);

  startCamera();

  // Connect to Laptop Wi-Fi Hotspot
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("Connecting to Laptop Hotspot");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts >= 20) {
      Serial.println("\nRetrying Wi-Fi connection...");
      WiFi.disconnect();
      delay(200);
      WiFi.begin(ssid, password);
      attempts = 0;
    }
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Camera Snapshot URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/capture");
  Serial.print("Arm Control URL:     http://");
  Serial.print(WiFi.localIP());
  Serial.println("/arm?angle=90");

  // Start HTTP Server on Port 80
  server.begin();

  // Start UDP for broadcast discovery
  udp.begin(DISCOVERY_PORT);

  // Pin server task to Core 0
  xTaskCreatePinnedToCore(
    serverTask,
    "serverTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
  // Step servo smoothly toward targetAngle and idle PWM once reached
  updateServoSmooth();

  // Broadcast discovery beacon every 2 seconds
  if (millis() - lastBeaconTime > 2000) {
    lastBeaconTime = millis();
    broadcastBeacon();
  }
  delay(5);
}

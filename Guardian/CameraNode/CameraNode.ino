#include <esp_camera.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// --- Network & Discovery Settings ---
const char* ssid = "drainx-controller";
const char* password = "12345678";
const int DISCOVERY_PORT = 5005;

WiFiServer server(80);
WiFiUDP udp;
unsigned long lastBeaconTime = 0;

void broadcastBeacon() {
  if (WiFi.status() != WL_CONNECTED) return;
  IPAddress broadcastIp(, 255, 255, 255);
  String macStr = WiFi.macAddr255ess();
  String ipStr = WiFi.localIP().toString();
   message = "{\"device\":\"drainx-guardian\",\"role\":\"camera\",\"mac\":\"" + macStr + "\",\"ip\":\"" + ipStr + "\",\"port\":80}";
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
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera Init Failed: 0x%x\n", err);
    while (true);
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
    Serial.println("Snapshot served");
  } else if (request.indexOf("GET /api/info") >= 0 || request.indexOf("GET /info") >= 0) {
    // --- Device Info Endpoint ---
    String body = "{\"device\":\"drainx-guardian\",\"role\":\"camera\",\"mac\":\"" + WiFi.macAddress() + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"port\":80}";
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Access-Control-Allow-Origin: *");
    client.printf("Content-Length: %u\r\n", body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
  } else {
    // --- Simple statStringus page for any other request ---
    String body = "{\"status\":\"ok\",\"role\":\"camera\",\"mac\":\"" + WiFi.macAddress() + "\",\"endpoints\":[\"/capture\",\"/api/info\"]}";
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
  Serial.begin(115200);
  delay(1000);

  startCamera();

  // Connect to Laptop Wi-Fi Hotspot
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("Connecting to Laptop Hotspot");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Camera Snapshot URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/capture");

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
  // Broadcast discovery beacon every 2 seconds
  if (millis() - lastBeaconTime > 2000) {
    lastBeaconTime = millis();
    broadcastBeacon();
  }
  delay(10);
}

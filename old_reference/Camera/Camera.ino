#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>

// --- Network & MQTT Settings ---
const char* ssid = "YOUR_LAPTOP_HOTSPOT_NAME";
const char* password = "YOUR_HOTSPOT_PASSWORD";
const char* mqtt_server = "192.168.137.1"; // Default Windows Hotspot IP (Use 192.168.2.1 on Mac / 10.42.0.1 on Linux)
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WiFiServer streamServer(81);

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

unsigned long lastSensorPublish = 0;

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

  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;   // 320x240
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera Init Failed: 0x%x\n", err);
    while (true);
  }
  Serial.println("Camera Ready");
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to Laptop MQTT Broker...");
    
    if (mqttClient.connect("ESP32CAM_Node")) {
      Serial.println(" Connected!");
      
      // Auto-announce connection & camera stream URL to laptop web server
      String statusPayload = "{\"status\":\"online\",\"ip\":\"" + WiFi.localIP().toString() + 
                             "\",\"stream_url\":\"http://" + WiFi.localIP().toString() + ":81/stream\"}";
      mqttClient.publish("esp32/status", statusPayload.c_str());
    } else {
      Serial.print(" Failed (rc=");
      Serial.print(mqttClient.state());
      Serial.println("). Retrying in 3 seconds...");
      delay(3000);
    }
  }
}

void streamVideo() {
  WiFiClient client = streamServer.available();
  if (!client) return;

  Serial.println("Stream Client Connected");

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println();

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Capture Failed");
      continue;
    }

    client.printf("--frame\r\n");
    client.printf("Content-Type: image/jpeg\r\n");
    client.printf("Content-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.printf("\r\n");

    esp_camera_fb_return(fb);
    delay(30);
  }

  Serial.println("Stream Client Disconnected");
}

void streamTask(void *pvParameters) {
  while (true) {
    streamVideo();
    vTaskDelay(1);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  startCamera();

  // Connect to Laptop Wi-Fi Hotspot
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Laptop Hotspot");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // Configure MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);

  // Start Video Stream Server on Port 81
  streamServer.begin();

  // Pin Video Streaming task to Core 0
  xTaskCreatePinnedToCore(
    streamTask,
    "streamTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Periodically read and push sensor readings via MQTT (every 2 seconds)
  unsigned long now = millis();
  if (now - lastSensorPublish > 2000) {
    lastSensorPublish = now;

    // TODO: Replace with your actual sensor readings (e.g. I2C/analog pin reads)
    float sampleTemp = 24.5;
    float sampleHumidity = 60.0;

    String jsonPayload = "{\"temperature\":" + String(sampleTemp) + 
                         ",\"humidity\":" + String(sampleHumidity) + "}";

    mqttClient.publish("esp32/sensors", jsonPayload.c_str());
    Serial.println("Pushed sensor payload: " + jsonPayload);
  }
}

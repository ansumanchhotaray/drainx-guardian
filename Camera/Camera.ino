#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// WiFi Credentials
const char* ssid = "Redmi Note 7 Pro_2891";
const char* password = "12345678";

WebServer server(80);
WiFiServer streamServer(81);

// AI Thinker ESP32-CAM Pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

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

  // Grayscale camera
  config.pixel_format = PIXFORMAT_GRAYSCALE;

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

void handleRoot() {

  String html =
  "<html><head>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<title>ESP32-CAM Stream</title>"
  "</head><body>"
  "<h2 align='center'>ESP32-CAM Live Stream</h2>"
  "<div align='center'>"
  "<img src='http://" + WiFi.localIP().toString() + ":81/stream'>"
  "</div>"
  "</body></html>";

  server.send(200, "text/html", html);
}

void streamVideo() {

  WiFiClient client = streamServer.available();

  if (!client) return;

  Serial.println("Client Connected");

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println();

  while (client.connected()) {

    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb) {
      Serial.println("Capture Failed");
      continue;
    }

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;

    bool converted = frame2jpg(fb, 40, &jpg_buf, &jpg_len);

    if (converted) {

      client.printf("--frame\r\n");
      client.printf("Content-Type: image/jpeg\r\n");
      client.printf("Content-Length: %u\r\n\r\n", jpg_len);

      client.write(jpg_buf, jpg_len);
      client.printf("\r\n");

      free(jpg_buf);
    }

    esp_camera_fb_return(fb);

    delay(30);
  }

  Serial.println("Client Disconnected");
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

  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");

  Serial.print("Open Browser: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.begin();

  streamServer.begin();

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
  server.handleClient();
}
#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiUdp.h>

// --- Network & Discovery Settings ---
const char* ssid = "drainx-controller";
const char* password = "12345678";
const int DISCOVERY_PORT = 5005;

WebServer server(80);
WiFiUDP udp;
unsigned long lastBeaconTime = 0;

void broadcastBeacon() {
  if (WiFi.status() != WL_CONNECTED) return;
  IPAddress broadcastIp(255, 255, 255, 255);
  String macStr = WiFi.macAddress();
  String ipStr = WiFi.localIP().toString();
  String message = "{\"device\":\"drainx-guardian\",\"role\":\"sensor\",\"mac\":\"" + macStr + "\",\"ip\":\"" + ipStr + "\",\"port\":80}";
  udp.beginPacket(broadcastIp, DISCOVERY_PORT);
  udp.print(message);
  udp.endPacket();
}

// --- Sensor Pins (Using ADC1 pins which are safe with Wi-Fi) ---
#define TURBIDITY_PIN 32
#define PH_SENSOR_PIN 35
#define ONE_WIRE_BUS 4  // Data pin for DS18B20 (temp)

// Initialize OneWire and DallasTemperature
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// pH Calibration
float ph_calibration_value = 13.59;

// --- Sensor Reading Functions ---

int readTurbidity() {
  int turbidityValue = analogRead(TURBIDITY_PIN);
  int calibration = (turbidityValue - 2717.6) / -18.189;
  return calibration;
}

float readPH() {
  int buffer_arr[10], temp;
  for (int i = 0; i < 10; i++) {
    buffer_arr[i] = analogRead(PH_SENSOR_PIN);
    delay(10);
  }
  // Bubble sort
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (buffer_arr[i] > buffer_arr[j]) {
        temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }
  unsigned long int avgval = 0;
  for (int i = 2; i < 8; i++) {
    avgval += buffer_arr[i];
  }
  float volt = (float)avgval * 3.3 / 4095.0 / 6;
  float ph_act = -5.70 * volt + ph_calibration_value;
  return ph_act;
}


float readTemperature() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  return tempC;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize Sensor Pins
  pinMode(TURBIDITY_PIN, INPUT);
  pinMode(PH_SENSOR_PIN, INPUT);

  // Initialize the DS18B20 sensor
  sensors.begin();

  // Connect to Laptop Wi-Fi Hotspot
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Laptop Hotspot");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Sensor Node IP: ");
  Serial.println(WiFi.localIP());

  // Start UDP for broadcast discovery
  udp.begin(DISCOVERY_PORT);

  // Setup Web Server Endpoints
  server.on("/api/info", []() {
    String json = "{\"device\":\"drainx-guardian\",\"role\":\"sensor\",\"mac\":\"" + WiFi.macAddress() + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"port\":80}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.on("/info", []() {
    String json = "{\"device\":\"drainx-guardian\",\"role\":\"sensor\",\"mac\":\"" + WiFi.macAddress() + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"port\":80}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.on("/sensors", []() {
    int turbidity = readTurbidity();
    String turbidityStatus = "Unknown";
    String turbidityClass = "status-normal";
    if(turbidity >= 0 && turbidity <= 49) {
      turbidityStatus = "Clear Water";
      turbidityClass = "status-normal";
    } else if (turbidity >= 50 && turbidity <= 75) {
      turbidityStatus = "Turbid";
      turbidityClass = "status-warning";
    } else if (turbidity >= 76 && turbidity <= 150) {
      turbidityStatus = "Very Turbid";
      turbidityClass = "status-danger";
    }

    float ph = readPH();
    float temperature = readTemperature();

    // Construct JSON payload
    String jsonPayload = "{";
    jsonPayload += "\"turbidity\":" + String(turbidity) + ",";
    jsonPayload += "\"turbidity_status\":\"" + turbidityStatus + "\",";
    jsonPayload += "\"turbidity_class\":\"" + turbidityClass + "\",";
    jsonPayload += "\"ph\":" + String(ph) + ",";
    jsonPayload += "\"temperature\":" + String(temperature);
    jsonPayload += "}";

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", jsonPayload);
    Serial.println("Served sensor payload: " + jsonPayload);
  });

  server.begin();
  Serial.println("HTTP Server started on port 80");
}

void loop() {
  // Handle incoming HTTP requests
  server.handleClient();

  // Broadcast discovery beacon every 2 seconds
  if (millis() - lastBeaconTime > 2000) {
    lastBeaconTime = millis();
    broadcastBeacon();
  }
  delay(2);
}
 
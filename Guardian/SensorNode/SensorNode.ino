#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- Network Settings ---
const char* ssid = "drainx-controller";
const char* password = "12345678";

WebServer server(80);

// --- Sensor Pins (Using ADC1 pins which are safe with Wi-Fi) ---
#define TURBIDITY_PIN 32
#define PH_SENSOR_PIN 35
#define ONE_WIRE_BUS 34  // Data pin for DS18B20 (temp)

// Initialize OneWire and DallasTemperature
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// pH Calibration
float ph_calibration_value = 13.59;

// --- Sensor Reading Functions ---

float readTurbidity() {
  int turbidityValue = analogRead(TURBIDITY_PIN);
  float calibration = (turbidityValue - 2717.6) / -18.189;
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

  // Error handling if sensor is unplugged or missing pull-up
  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("WARNING: DS18B20 temperature sensor disconnected! Check wiring and pull-up resistor (4.7k).");
    return -999.0;
  }

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
  Serial.print("Sensor Node IP: ");
  Serial.println(WiFi.localIP());

  // Setup Web Server Endpoints
  server.on("/sensors", []() {
    float turbidity = readTurbidity();
    float ph = readPH();
    float temperature = readTemperature();

    // Construct JSON payload
    String jsonPayload = "{";
    jsonPayload += "\"turbidity\":" + String(turbidity) + ",";
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
  delay(2);
}

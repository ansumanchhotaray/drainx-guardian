#include <WiFi.h>
#include <HTTPClient.h>

// --- Network Settings ---
const char* ssid = "YOUR_LAPTOP_HOTSPOT_NAME";
const char* password = "YOUR_HOTSPOT_PASSWORD";

// The IP address of your laptop running the Python server.
// Default below is the Windows Mobile Hotspot gateway IP.
// On Linux/Mac, replace this with your actual hotspot/network IP (check with `ip addr` or `ifconfig`).
const char* serverName = "http://192.168.137.1:8000/api/sensors";

// --- Sensor Pins (Using ADC1 pins which are safe with Wi-Fi) ---
#define TURBIDITY_PIN 32
#define PH_SENSOR_PIN 33
#define TEMP_SENSOR_PIN 34 // Placeholder for analog temp sensor
#define TRIG_PIN 5
#define ECHO_PIN 18

unsigned long lastSensorPublish = 0;

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

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = duration * 0.034 / 2;
  return distance;
}

float readTemperature() {
  // Placeholder for analog temperature sensor
  int tempValue = analogRead(TEMP_SENSOR_PIN);
  float voltage = tempValue * (3.3 / 4095.0);
  float temperature = voltage * 100.0; // Example conversion
  return temperature;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize Sensor Pins
  pinMode(TURBIDITY_PIN, INPUT);
  pinMode(PH_SENSOR_PIN, INPUT);
  pinMode(TEMP_SENSOR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

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
}

void loop() {
  // Periodically read and push sensor readings via HTTP POST (every 2 seconds)
  unsigned long now = millis();
  if (now - lastSensorPublish > 2000) {
    lastSensorPublish = now;

    if(WiFi.status() == WL_CONNECTED) {
      float turbidity = readTurbidity();
      float ph = readPH();
      float distance = readUltrasonic();
      float temperature = readTemperature();

      // Construct JSON payload
      String jsonPayload = "{";
      jsonPayload += "\"turbidity\":" + String(turbidity) + ",";
      jsonPayload += "\"ph\":" + String(ph) + ",";
      jsonPayload += "\"water_level_cm\":" + String(distance) + ",";
      jsonPayload += "\"temperature\":" + String(temperature);
      jsonPayload += "}";

      HTTPClient http;
      http.begin(serverName);
      http.addHeader("Content-Type", "application/json");
      
      int httpResponseCode = http.POST(jsonPayload);
      
      if (httpResponseCode > 0) {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
      } else {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
      }
      http.end();
      
      Serial.println("Pushed sensor payload: " + jsonPayload);
    } else {
      Serial.println("WiFi Disconnected");
    }
  }
}

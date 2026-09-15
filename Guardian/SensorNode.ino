#include <WiFi.h>
#include <PubSubClient.h>

// --- Network & MQTT Settings ---
const char* ssid = "YOUR_LAPTOP_HOTSPOT_NAME";
const char* password = "YOUR_HOTSPOT_PASSWORD";
const char* mqtt_server = "192.168.137.1"; // Default Windows Hotspot IP
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- Sensor Pins (Using ADC1 pins which are safe with Wi-Fi) ---
#define TURBIDITY_PIN 32
#define PH_SENSOR_PIN 33
#define TEMP_SENSOR_PIN 34 // Placeholder for analog temp sensor
#define TRIG_PIN 5
#define ECHO_PIN 18

unsigned long lastSensorPublish = 0;

// pH Calibration
float ph_calibration_value = 13.59;

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to Laptop MQTT Broker...");
    
    if (mqttClient.connect("ESP32_Sensor_Node")) {
      Serial.println(" Connected!");
      
      String statusPayload = "{\"status\":\"online\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
      mqttClient.publish("esp32/sensors/status", statusPayload.c_str());
    } else {
      Serial.print(" Failed (rc=");
      Serial.print(mqttClient.state());
      Serial.println("). Retrying in 3 seconds...");
      delay(3000);
    }
  }
}

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

  // Configure MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
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

    mqttClient.publish("esp32/sensors", jsonPayload.c_str());
    Serial.println("Pushed sensor payload: " + jsonPayload);
  }
}

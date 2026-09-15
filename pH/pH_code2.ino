// Replace with your Blynk credentials
#define BLYNK_TEMPLATE_ID "TMPL3dn3E-Pcn"
#define BLYNK_TEMPLATE_NAME "pH Monitor"
#define BLYNK_AUTH_TOKEN "4C5pNgUXWX2Ci7FkigKMhM12fnNzLET1"

#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

// Replace with your WiFi details
char ssid[] = "moto g96 5G_4551";
char pass[] = "indumahi09";

float calibration_value = 13.59; // Fixed standard calibration baseline
unsigned long int avgval;
int buffer_arr[10], temp;
float ph_act;

#define PH_SENSOR_PIN 35

BlynkTimer timer;

// This function processes the sensor data and sends it to Blynk
void sendSensorData() {
  // Read analog data 10 times
  for (int i = 0; i < 10; i++) {
    buffer_arr[i] = analogRead(PH_SENSOR_PIN);
    delay(30);
  }

  // Bubble sort the data to remove outliers
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (buffer_arr[i] > buffer_arr[j]) {
        temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }

  // Get average of the middle 6 readings
  avgval = 0;
  for (int i = 2; i < 8; i++) {
    avgval += buffer_arr[i];
  }

  // Convert average ADC value to voltage (ESP32: 3.3V / 4095)
  float volt = (float)avgval * 3.3 / 4095.0 / 6;
  
  // Calculate pH value using linear slope formula
  ph_act = -5.70 * volt + calibration_value;

  // Print to Serial Monitor
  Serial.print("pH Value: ");
  Serial.println(ph_act);

  // Send the pH value to Blynk Virtual Pin V0
  Blynk.virtualWrite(V0, ph_act);
}

void setup() {
  Serial.begin(115200);
  
  // Set GPIO 34 as analog input
  pinMode(PH_SENSOR_PIN, INPUT);

  // Connect to WiFi and Blynk Server
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Call sendSensorData() every 2000 ms (2 seconds)
  timer.setInterval(2000L, sendSensorData);
}

void loop() {
  Blynk.run();
  timer.run();
}
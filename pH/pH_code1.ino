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

int pH_Value;
float Voltage;

BlynkTimer timer;

// This function replaces the loop() logic to work safely with Blynk
void sendData() {
  pH_Value = analogRead(35);
  Voltage = pH_Value * (3.3 / 4095.0);
  
  Serial.print("Voltage: ");
  Serial.println(Voltage);
  
  // Send the voltage reading to Blynk Virtual Pin V0
  Blynk.virtualWrite(V0, Voltage);
}

void setup() {
  Serial.begin(115200); // Standard baud rate for ESP32/Blynk
  pinMode(34, INPUT);   // Fixed pin assignment
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  // Call sendData() every 1000 milliseconds (1 second)
  timer.setInterval(1000L, sendData);
}

void loop() {
  Blynk.run();
  timer.run();
}
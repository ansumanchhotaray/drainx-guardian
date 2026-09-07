// #include <LiquidCrystal.h>


#define BLYNK_TEMPLATE_ID "TMPL3B_hQEFdF"
#define BLYNK_TEMPLATE_NAME "Turbidity"
#define BLYNK_AUTH_TOKEN "dZvzcnjIv5F28XLhE_F3ew-NyofxVlaA"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
// #include <LiquidCrystal_I2C.h>
// LiquidCrystal_I2C lcd(0x27, 16, 2);

#define TURBIDITY_PIN 34  

BlynkTimer timer;

char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "Capasitor_Tech";
char pass[] = "Kopisusu26";

void setup() {
  Serial.begin(115200);       
  analogReadResolution(12);   
  // lcd.begin();
  // lcd.backlight();
  Blynk.begin(auth, ssid, pass);
  timer.setInterval(500L, Turbidity);
}

void loop() 
{
  Blynk.run();
  timer.run();
}

void Turbidity() {
  int turbidityValue = analogRead(TURBIDITY_PIN);
  int kal = (turbidityValue - 2717.6) / -18.189;
  String status;
  Serial.print("Turbidity ADC Value: ");
  Serial.print(turbidityValue);
  Serial.print(" | KAL: ");
  Serial.print(kal);
  Blynk.virtualWrite(V0, kal);
  if(kal > 0 && kal < 49)
  {
    status = "Air Jernih";
  } else if (kal > 50 && kal < 75){
    status = "Keruh";
  } else if (kal > 76 && kal < 150){
    status = "Sangat Keruh";
  }
  Blynk.virtualWrite(V1, status);
  // lcd.setCursor(0, 0);
  // lcd.print("Nilai = ");
  // lcd.print(kal);
  // lcd.print("       ");
  // lcd.setCursor(0, 1);
  // lcd.print(status);
  // lcd.print("        ");
}



#define BLYNK_TEMPLATE_ID "TMPL3B_hQEFdF"
#define BLYNK_TEMPLATE_NAME "Turbidity"
#define BLYNK_AUTH_TOKEN "dZvzcnjIv5F28XLhE_F3ew-NyofxVlaA"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define TURBIDITY_PIN 34  

BlynkTimer timer;

char auth[] = BLYNK_AUTH_TOKEN;

char ssid[] = "moto g96 5G_4551";
char pass[] = "indumahi09";

void setup() {
  Serial.begin(115200);       
  analogReadResolution(12);   
  lcd.init();
  lcd.backlight();
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
  int Calibration = (turbidityValue - 2717.6) / -18.189;
  String status;
  Serial.print("Turbidity ADC Value: ");
  Serial.print(turbidityValue);
  Serial.print(" | Calibration: ");
  Serial.println(Calibration);
  Blynk.virtualWrite(V2, Calibration);
  if(Calibration >= 0 && Calibration <= 49)
  {
    status = "Clear Water";
  } else if (Calibration >= 50 && Calibration <= 75){
    status = "Turbid";
  } else if (Calibration >= 76 && Calibration <= 150){
    status = "Very Turbid";
  }
  Blynk.virtualWrite(V1, status);
  lcd.setCursor(0, 0);
  lcd.print("Value = ");
  lcd.print(Calibration);
  lcd.print("       ");
  lcd.setCursor(0, 1);
  lcd.print(status);
  lcd.print("        ");
}

# ESP32-CAM Wiring & Connection Guide

This guide explains how to connect your sensors to the ESP32-CAM based on the integrated `Guardian/Main.ino` code.

## ⚠️ CRITICAL HARDWARE LIMITATION (Please Read First)
The ESP32-CAM has very few available GPIO pins because most are used by the camera and the SD card reader. 
The pins we have assigned for the analog sensors (GPIO 13, 14, 15) belong to **ADC2**. 
**On the ESP32 architecture, ADC2 cannot be used for `analogRead()` while Wi-Fi is active.** Because this project streams video and sends MQTT data over Wi-Fi, reading the Turbidity, pH, and Temperature sensors directly on these pins will likely fail or return invalid data.

### Recommended Solutions:
1. **Best Option (External ADC)**: Buy an **ADS1115 I2C ADC module**. Connect the analog sensors to the ADS1115, and connect the ADS1115 to the ESP32-CAM using I2C (SDA/SCL).
2. **Alternative Option (Two Boards)**: Use the ESP32-CAM **only** for the camera stream, and use a standard **ESP32 NodeMCU** (which has plenty of ADC1 pins) to read all the sensors and send the MQTT data.

---

## Direct Wiring Guide (Based on Current Code)
If you want to wire it up exactly as the code is currently written (for testing purposes), here are the connections:

### Power Connections
*Note: It is highly recommended to power the ESP32-CAM using a dedicated 5V power supply (like an MB102 breadboard power supply) rather than your computer's USB, as the camera and sensors draw a lot of current.*
- **ESP32-CAM 5V** -> Connect to the `VCC` or `5V` pin of the Ultrasonic, Turbidity, and pH sensors.
- **ESP32-CAM GND** -> Connect to the `GND` pin of **ALL** sensors.

### 1. Ultrasonic Sensor (HC-SR04)
- **VCC** -> 5V
- **GND** -> GND
- **TRIG** -> **GPIO 12** on ESP32-CAM
- **ECHO** -> **GPIO 16** on ESP32-CAM
  - *Safety Note*: The HC-SR04 outputs a 5V signal on the ECHO pin, but the ESP32 expects 3.3V. It is highly recommended to use a voltage divider (e.g., a 1kΩ and 2kΩ resistor) on the ECHO pin to drop the voltage to 3.3V to prevent damaging the ESP32.

### 2. Turbidity Sensor
- **VCC** -> 5V
- **GND** -> GND
- **OUT (Analog)** -> **GPIO 13** on ESP32-CAM

### 3. pH Sensor
- **VCC** -> 5V
- **GND** -> GND
- **PO (Analog Out)** -> **GPIO 14** on ESP32-CAM

### 4. Temperature Sensor (Analog Placeholder)
- **VCC** -> 5V or 3.3V (depending on sensor)
- **GND** -> GND
- **OUT (Analog)** -> **GPIO 15** on ESP32-CAM

---

## How to Upload the Code to ESP32-CAM
The ESP32-CAM does not have a built-in USB port. To upload the code, you need an **FTDI Programmer (USB to TTL converter)**.

1. Connect the FTDI programmer to the ESP32-CAM:
   - FTDI `5V` -> ESP32-CAM `5V`
   - FTDI `GND` -> ESP32-CAM `GND`
   - FTDI `TX` -> ESP32-CAM `U0R` (GPIO 3)
   - FTDI `RX` -> ESP32-CAM `U0T` (GPIO 1)
2. **Put ESP32-CAM in Flash Mode**: Connect **GPIO 0 to GND** on the ESP32-CAM.
3. Plug the FTDI into your computer.
4. In the Arduino IDE, select **AI Thinker ESP32-CAM** as the board.
5. Click Upload.
6. Once uploading is done, **disconnect GPIO 0 from GND** and press the RESET button on the ESP32-CAM to start the code.

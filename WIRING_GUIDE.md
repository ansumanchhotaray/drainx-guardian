# Two-Board Wiring & Connection Guide

Since you have both an **ESP32-CAM** and a regular **ESP32 WROOM**, the best and most reliable approach is to use a **Two-Board Architecture**. 

This completely solves the ESP32-CAM's ADC2/Wi-Fi limitation. The ESP32-CAM will handle the heavy lifting of video streaming, and the ESP32 WROOM will handle the precise analog sensor readings. Both will connect to your laptop's Wi-Fi hotspot.

---

## 1. Camera Node (ESP32-CAM)
The ESP32-CAM is dedicated entirely to streaming video.
- **Code**: Upload `Guardian/CameraNode.ino` to this board.
- **Wiring**: No sensors are connected to this board. Just provide it with stable 5V power.

---

## 2. Sensor Node (ESP32 WROOM)
The ESP32 WROOM reads all the sensors and sends the data via MQTT.
- **Code**: Upload `Guardian/SensorNode.ino` to this board.
- **Wiring**: We will use the **ADC1** pins on the ESP32 WROOM, which work perfectly even when Wi-Fi is turned on.

### Power Connections
- **ESP32 5V (VIN)** -> Connect to the `VCC` or `5V` pin of the Ultrasonic, Turbidity, and pH sensors.
- **ESP32 GND** -> Connect to the `GND` pin of **ALL** sensors.

### Sensor Data Connections

#### 1. Ultrasonic Sensor (HC-SR04)
- **VCC** -> 5V
- **GND** -> GND
- **TRIG** -> **GPIO 5**
- **ECHO** -> **GPIO 18**
  - *Safety Note*: The HC-SR04 outputs a 5V signal on the ECHO pin, but the ESP32 expects 3.3V. It is highly recommended to use a voltage divider (e.g., a 1kΩ and 2kΩ resistor) on the ECHO pin to drop the voltage to 3.3V to prevent damaging the ESP32.

#### 2. Turbidity Sensor
- **VCC** -> 5V
- **GND** -> GND
- **OUT (Analog)** -> **GPIO 32** (ADC1_CH4)

#### 3. pH Sensor
- **VCC** -> 5V
- **GND** -> GND
- **PO (Analog Out)** -> **GPIO 33** (ADC1_CH5)

#### 4. Temperature Sensor (Analog Placeholder)
- **VCC** -> 5V or 3.3V (depending on sensor)
- **GND** -> GND
- **OUT (Analog)** -> **GPIO 34** (ADC1_CH6)

---

## How to Upload the Code

### For the ESP32 WROOM (Sensor Node):
1. Plug it directly into your computer via USB.
2. Select **DOIT ESP32 DEVKIT V1** (or similar) in the Arduino IDE.
3. Upload `Guardian/SensorNode.ino`.

### For the ESP32-CAM (Camera Node):
You need an **FTDI Programmer (USB to TTL converter)**.
1. Connect the FTDI programmer to the ESP32-CAM:
   - FTDI `5V` -> ESP32-CAM `5V`
   - FTDI `GND` -> ESP32-CAM `GND`
   - FTDI `TX` -> ESP32-CAM `U0R` (GPIO 3)
   - FTDI `RX` -> ESP32-CAM `U0T` (GPIO 1)
2. **Put ESP32-CAM in Flash Mode**: Connect **GPIO 0 to GND** on the ESP32-CAM.
3. Plug the FTDI into your computer.
4. Select **AI Thinker ESP32-CAM** in the Arduino IDE.
5. Upload `Guardian/CameraNode.ino`.
6. Once uploading is done, **disconnect GPIO 0 from GND** and press the RESET button on the ESP32-CAM to start the code.

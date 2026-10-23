# Two-Board Wiring & Connection Guide

Since you have both an **ESP32-CAM** and a regular **ESP32 WROOM**, the best and most reliable approach is to use a **Two-Board Architecture**. 

This completely solves the ESP32-CAM's ADC2/Wi-Fi limitation. The ESP32-CAM will handle the heavy lifting of video streaming, and the ESP32 WROOM will handle the precise analog sensor readings. Both will connect to your laptop's Wi-Fi hotspot.

---

## 1. Camera & Arm Node (AI-Thinker ESP32-CAM + Servo Arm)
The ESP32-CAM runs `Guardian/CameraAndArmNode/CameraAndArmNode.ino` (also mirrored in `Guardian/CameraNode/CameraNode.ino`), serving both the live camera snapshots (`/capture`) and the robotic debris-collector servo arm (`/arm?angle=0..180`) over Wi-Fi LAN.
- **Code**: Upload `Guardian/CameraAndArmNode/CameraAndArmNode.ino` to the **AI-Thinker ESP32-CAM**.
- **Auto-Discovery**: Broadcasts UDP beacons on port 5005 (`role: "camera_arm"`), automatically bringing both the **Camera** and **Collector Arm** panels online in the web dashboard.

### ESP32-CAM Header Pinout (Back View — `ESP-32S` Shield Facing You, Antenna at Bottom)
```text
      LEFT HEADER                           RIGHT HEADER
  ┌─────────────────┐                   ┌─────────────────┐
  │ 1. 3V3          │                   │ 1. 5V   (Red)   │ <-- +5V Power Input
  │ 2. IO16         │                   │ 2. GND  (Brown) │ <-- Common Ground
  │ 3. IO0  (Flash) │                   │ 3. IO12 (Skip*) │
  │ 4. GND          │    ┌─────────┐    │ 4. IO13 (SERVO) │ <-- Servo Signal (Orange/Yellow)
  │ 5. VCC          │    │ ESP-32S │    │ 5. IO15         │
  │ 6. U0R  (FTDI)  │    └─────────┘    │ 6. IO14 (Backup)│ <-- Backup Servo Signal
  │ 7. U0T  (FTDI)  │     ┌───────┐     │ 7. IO2          │
  │ 8. GND  (Spare) │     │ANTENNA│     │ 8. IO4  (Flash) │
  └─────────────────┘     └───────┘     └─────────────────┘
```
*\*Note: Skip `IO12` (`GPIO 12`) because it is an ESP32 boot strapping pin — pulling it high at boot prevents the ESP32 from starting.*

### Servo 3-Wire Connections to ESP32-CAM
1. **Servo Signal (Orange / Yellow wire)** $\rightarrow$ **`IO13` (`GPIO 13`)** — the **4th pin down on the Right Header** (2 pins below your brown `GND` wire).
   - *Note*: **`IO14` (`GPIO 14`)** (6th pin down on the Right Header) is also driven with the exact same 50 Hz servo signal in parallel, so either `IO13` or `IO14` works.
2. **Servo VCC (Red wire)** $\rightarrow$ **External 5V Power Rail** (shared with the `5V` source powering top-right Pin 1 of the ESP32-CAM; avoid powering a loaded servo exclusively through a weak USB-TTL 3.3V regulator so the camera doesn't brown out when the arm moves).
3. **Servo GND (Brown / Black wire)** $\rightarrow$ **Common GND** (connect to the spare **bottom-left `GND` pin (Pin 8 on Left Header)** on the ESP32-CAM, or directly to the shared power supply `GND`).

---

## 2. Sensor Node (ESP32 WROOM)
The ESP32 WROOM reads all the sensors and sends the data directly to the Python server via HTTP POST.
- **Code**: Upload `Guardian/SensorNode.ino` to this board.
- **Wiring**: We will use the **ADC1** pins on the ESP32 WROOM, which work perfectly even when Wi-Fi is turned on.

### Power Connections
- **ESP32 5V (VIN)** -> Connect to the `VCC` or `5V` pin of the Turbidity, and pH sensors.
- **ESP32 GND** -> Connect to the `GND` pin of **ALL** sensors.

### Sensor Data Connections

#### 1. Turbidity Sensor
- **VCC** -> 5V
- **GND** -> GND
- **OUT (Analog)** -> **GPIO 32** (ADC1_CH4)

#### 2. pH Sensor
- **VCC** -> 5V
- **GND** -> GND
- **PO (Analog Out)** -> **GPIO 35** (ADC1_CH5)

#### 3. Temperature Sensor (DS18B20)
- **VCC** -> 3.3V or 5V
- **GND** -> GND
- **DATA** -> **GPIO 4** (Requires 4.7kΩ pull-up resistor to VCC)

---

## 3. Motor Node (ESP32 WROOM + L298N)
The Motor Node receives driving commands (forward, backward, left, right, stop) from the server and drives the wheels.
- **Code**: Upload `Guardian/MotorNode/MotorNode.ino` to this board.
- **Auto-Discovery**: Sends UDP broadcast beacons on port 5005 (`role: "motor"`), automatically appearing on the web dashboard.

### Pinout (L298N Dual H-Bridge)
- **IN1** -> **GPIO 14** (Channel A — Left Side)
- **IN2** -> **GPIO 27**
- **IN3** -> **GPIO 26** (Channel B — Right Side)
- **IN4** -> **GPIO 18** (Channel B — Right Side)
- **ENA** -> **GPIO 33** (PWM Speed)
- **ENB** -> **GPIO 32** (PWM Speed)

### Power Connections
- **L298N 12V** -> Battery (+) [7.4V - 12V]
- **L298N GND** -> Battery (-) AND ESP32 GND (Common Ground is mandatory)
- **ESP32 5V/VIN** -> L298N 5V Out (or separate 5V regulator/USB)

---

## How to Upload the Code

### For the ESP32 WROOM (Sensor Node / Motor Node):
1. Plug it directly into your computer via USB.
2. Select **DOIT ESP32 DEVKIT V1** (or similar) in the Arduino IDE.
3. Upload `Guardian/SensorNode/SensorNode.ino` or `Guardian/MotorNode/MotorNode.ino`.

### For the ESP32-CAM (CameraAndArmNode):
You need an **FTDI Programmer (USB to TTL converter)**.
1. Connect the FTDI programmer to the ESP32-CAM:
   - FTDI `5V` -> ESP32-CAM `5V` (Top-right Pin 1)
   - FTDI `GND` -> ESP32-CAM `GND` (Top-right Pin 2)
   - FTDI `TX` -> ESP32-CAM `U0R` (GPIO 3, Left Header Pin 6)
   - FTDI `RX` -> ESP32-CAM `U0T` (GPIO 1, Left Header Pin 7)
2. **Put ESP32-CAM in Flash Mode**: Connect **`IO0` (GPIO 0) to `GND`** on the Left Header (Pins 3 & 4).
3. Plug the FTDI into your computer.
4. Select **AI Thinker ESP32-CAM** in the Arduino IDE.
5. Upload `Guardian/CameraAndArmNode/CameraAndArmNode.ino`.
6. Once uploading is done, **disconnect `IO0` from `GND`** and press the **RESET** button on the ESP32-CAM to start the code.

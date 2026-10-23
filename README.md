# DrainX Guardian

An autonomous drainage monitoring system that uses a two-board ESP32 architecture to stream live video and monitor water quality (Turbidity, pH, and Temperature) in real-time.

## Architecture Overview

This project uses a modular multi-node architecture to ensure maximum stability and bypass hardware limitations on individual ESP32 boards:
1. **Camera Node (ESP32-CAM)**: Dedicated entirely to streaming video snapshots (`/capture`).
2. **Sensor Node (ESP32 WROOM)**: Dedicated to reading analog sensors (Turbidity, pH, Temperature) and serving data (`/sensors`).
3. **Motor Node (ESP32 WROOM)**: Dedicated to robot propulsion via L298N dual H-bridge motor driver (`/move`, `/speed`, `/status`).
4. **Arm Node (Arduino Uno)**: Dedicated to robotic debris collection arm servo on Pin 9 via USB Serial (`/api/arm`).
5. **Backend Server (Python/FastAPI)**: Auto-discovers all nodes via UDP beacons on port 5005 & USB Serial auto-detection, proxies AI vision, streams telemetry via WebSockets (`/ws`), and dispatches motor and arm commands.
6. **Frontend Dashboard**: A modern, glassmorphic UI displaying the live camera feed, real-time sensor telemetry, directional drive controls, and robotic arm angle slider.

## 1. Hardware Setup (ESP32)

Please refer to the `WIRING_GUIDE.md` file in this repository for exact pinouts and power connections for both boards.

1. Open `Guardian/CameraNode.ino` in the Arduino IDE.
2. Change the `ssid` and `password` to match your laptop's Mobile Hotspot.
3. Upload to your **ESP32-CAM**.
4. Open `Guardian/SensorNode.ino` in the Arduino IDE.
5. Change the `ssid`, `password`, and `serverName` to match your laptop's hotspot IP.
   - **Windows** (Mobile Hotspot): The default IP is usually `192.168.137.1` — check in your hotspot settings.
   - **Linux/Mac**: Run `ip addr` or `ifconfig` to find the IP of your hotspot interface.
6. Upload to your **ESP32 WROOM**.

## 2. Server Setup (Windows / Linux / Mac)

The backend is a lightweight Python FastAPI server. It does **not** require any external message brokers like Mosquitto.

### Prerequisites
Make sure you have Python 3.8+ installed.

### Installation
Open a terminal in the project directory and run:
```bash
cd server
python -m venv .venv
source .venv/bin/activate  # On Windows use: .venv\Scripts\activate
pip install -r requirements.txt
```

### Running the Server
Start the FastAPI server by running:
```bash
python app.py
```

The server will start on `http://0.0.0.0:8000`.

### Running the Local Webcam Demo
If you want to test the YOLOv8 garbage detection locally using your laptop's webcam, run:
```bash
python webcam_demo.py
```

## 3. Viewing the Dashboard

1. Open your web browser and navigate to: [http://localhost:8000](http://localhost:8000)
2. You will see the DrainX Guardian dashboard with real-time node discovery status bars.
3. **Plug & Play Auto-Discovery**: As soon as your ESP32-CAM and ESP32 WROOM connect to the Wi-Fi network, they broadcast their node role and physical MAC address via UDP beacons.
4. The server automatically detects each board, tracks its physical MAC address, starts camera stream processing, and begins telemetry polling without requiring you to manually enter IP addresses.
5. If needed, a "Manual IP Override" dropdown is available for debugging or custom network routing.

## Folder Structure
- `/Guardian`: Contains the Arduino `.ino` sketches for the ESP32 boards.
- `/server`: Contains the Python FastAPI backend and the HTML/CSS/JS frontend.
- `/old_reference`: Contains the original, un-integrated sensor test codes.
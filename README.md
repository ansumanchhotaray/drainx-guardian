# DrainX Guardian

An autonomous drainage monitoring and debris-collection system that uses a three-node ESP32 architecture to stream live video, detect floating garbage using zero-shot AI vision (YOLO-World), actuate a robotic collector arm and propulsion motors, and monitor water quality (Turbidity, pH, and Temperature) in real time.

## Architecture Overview

This project uses a modular multi-node architecture to ensure maximum stability and bypass hardware limitations on individual ESP32 boards:
1. **Camera & Arm Node (`CameraAndArmNode`, AI-Thinker ESP32-CAM)**: Streams live video snapshots (`/capture`) and drives the robotic debris-collection servo arm on `GPIO 13` / `GPIO 14` (`/arm?angle=0..180`) over Wi-Fi LAN.
2. **Sensor Node (`SensorNode`, ESP32 WROOM)**: Dedicated to reading water quality sensors (Turbidity on `GPIO 32`, pH on `GPIO 35`, and DS18B20 Temperature on `GPIO 4`) and serving live JSON telemetry (`/sensors`).
3. **Motor Node (`MotorNode`, ESP32 WROOM)**: Dedicated to robot propulsion via an L298N dual H-bridge motor driver (`/move`, `/speed`, `/invert`, `/status`).
4. **Backend Server (Python/FastAPI)**: Auto-discovers all nodes via UDP beacons on port `5005`, runs zero-shot YOLO-World (`yolov8s-worldv2.pt`) garbage detection, coordinates the Autonomous Garbage Collection state machine (`/api/auto`), streams telemetry via WebSockets (`/ws`), and dispatches motor and arm commands over LAN.
5. **Frontend Dashboard**: A modern UI displaying the live camera feed with real-time bounding-box overlays, water quality sensor telemetry, autonomous mode controls, directional drive controls, and robotic arm angle slider.

## 1. Hardware Setup (ESP32)

Please refer to the [WIRING_GUIDE.md](WIRING_GUIDE.md) file in this repository for exact pinouts and power connections for all three boards.

1. Configure your laptop's Wi-Fi Mobile Hotspot (default credentials in all sketches: SSID `drainx-controller`, password `12345678`), or update `ssid` and `password` at the top of each `.ino` sketch to match your hotspot.
2. **Camera & Arm Node**:
   - Open `Guardian/CameraAndArmNode/CameraAndArmNode.ino` in the Arduino IDE.
   - Select **AI Thinker ESP32-CAM** and upload via an FTDI USB-to-TTL programmer.
3. **Sensor Node**:
   - Open `Guardian/SensorNode/SensorNode.ino` in the Arduino IDE.
   - Select **DOIT ESP32 DEVKIT V1** (or your ESP32 WROOM board) and upload.
4. **Motor Node**:
   - Open `Guardian/MotorNode/MotorNode.ino` in the Arduino IDE.
   - Select **DOIT ESP32 DEVKIT V1** (or your ESP32 WROOM board) and upload.

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
If you want to test the YOLO-World (`yolov8s-worldv2.pt`) garbage detection locally using your laptop's webcam, run:
```bash
python webcam_demo.py
```

## 3. Viewing the Dashboard

1. Open your web browser and navigate to: [http://localhost:8000](http://localhost:8000)
2. You will see the DrainX Guardian dashboard with real-time node discovery status bars.
3. **Plug & Play Auto-Discovery**: As soon as your ESP32-CAM and ESP32 WROOM boards connect to the Wi-Fi network, they broadcast their node role (`camera_arm`, `sensor`, `motor`) and physical MAC address via UDP beacons on port `5005`.
4. The server automatically detects each board, tracks its physical MAC address, starts camera stream processing, and begins telemetry polling without requiring you to manually enter IP addresses.
5. If needed, a "Manual IP Override" dropdown is available on each panel for debugging or custom network routing.

## Folder Structure
- `/Guardian`: Contains the Arduino `.ino` sketches for the three ESP32 nodes (`CameraAndArmNode`, `SensorNode`, `MotorNode`).
- `/server`: Contains the Python FastAPI backend (`app.py`, `vision.py`, `webcam_demo.py`) and the HTML/CSS/JS frontend (`/static`).
- `/old_reference`: Contains the original, standalone sensor and actuator prototype sketches.
- `WIRING_GUIDE.md`: Complete pinout diagrams and power wiring instructions for all three ESP32 nodes.
- `REFERENCES.md`: Academic papers, datasets, libraries, hardware datasheets, and tutorials referenced in this codebase.
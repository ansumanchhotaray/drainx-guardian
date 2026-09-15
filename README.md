# DrainX Guardian

An autonomous drainage monitoring system that uses a two-board ESP32 architecture to stream live video and monitor water quality (Turbidity, pH, Temperature, and Water Level) in real-time.

## Architecture Overview

This project uses a **Two-Board Architecture** to ensure maximum stability and bypass hardware limitations on the ESP32-CAM:
1. **Camera Node (ESP32-CAM)**: Dedicated entirely to streaming video over HTTP.
2. **Sensor Node (ESP32 WROOM)**: Dedicated to reading analog sensors and sending data to the server via HTTP POST.
3. **Backend Server (Python/FastAPI)**: Receives the HTTP POST data and broadcasts it to the frontend via WebSockets.
4. **Frontend Dashboard**: A modern, glassmorphic UI that displays the live video stream and real-time sensor data.

## 1. Hardware Setup (ESP32)

Please refer to the `WIRING_GUIDE.md` file in this repository for exact pinouts and power connections for both boards.

1. Open `Guardian/CameraNode.ino` in the Arduino IDE.
2. Change the `ssid` and `password` to match your laptop's Mobile Hotspot.
3. Upload to your **ESP32-CAM**.
4. Open `Guardian/SensorNode.ino` in the Arduino IDE.
5. Change the `ssid`, `password`, and `serverName` (ensure the IP matches your laptop's hotspot IP).
6. Upload to your **ESP32 WROOM**.

## 2. Server Setup (Windows / Linux / Mac)

The backend is a lightweight Python FastAPI server. It does **not** require any external message brokers like Mosquitto.

### Prerequisites
Make sure you have Python 3.8+ installed.

### Installation
Open a terminal in the project directory and run:
```bash
cd server
pip install -r requirements.txt
```

### Running the Server
Start the FastAPI server by running:
```bash
python app.py
```

The server will start on `http://0.0.0.0:8000`.

## 3. Viewing the Dashboard

1. Open your web browser and navigate to: [http://localhost:8000](http://localhost:8000)
2. You will see the DrainX Guardian dashboard.
3. Once your ESP32 WROOM connects to the Wi-Fi, the sensor values will automatically update on the screen every 2 seconds.
4. Enter the IP address of your ESP32-CAM (printed in the Arduino Serial Monitor) into the "Connect Stream" input box to view the live video feed.

## Folder Structure
- `/Guardian`: Contains the Arduino `.ino` sketches for the ESP32 boards.
- `/server`: Contains the Python FastAPI backend and the HTML/CSS/JS frontend.
- `/old_reference`: Contains the original, un-integrated sensor test codes.
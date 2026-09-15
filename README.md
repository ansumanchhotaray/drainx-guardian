# DrainX Guardian

An autonomous drainage monitoring system that uses a two-board ESP32 architecture to stream live video and monitor water quality (Turbidity, pH, Temperature, and Water Level) in real-time.

## Architecture Overview

This project uses a **Two-Board Architecture** to ensure maximum stability and bypass hardware limitations on the ESP32-CAM:
1. **Camera Node (ESP32-CAM)**: Dedicated entirely to streaming MJPEG video over HTTP (port 81).
2. **Sensor Node (ESP32 WROOM)**: Dedicated to reading analog sensors and pushing data to the server via HTTP POST.
3. **Backend Server (Python/FastAPI)**: Receives sensor data (POST `/api/sensors`), proxies and processes the camera MJPEG stream for garbage detection (`/video_feed`), and broadcasts sensor data to the frontend via WebSockets (`/ws`).
4. **Frontend Dashboard**: A modern, glassmorphic UI that displays the processed live video stream and real-time sensor data.

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
4. Enter the IP address of your ESP32-CAM (printed in the Arduino Serial Monitor) into the "Camera IP" input box and click Connect. The frontend will notify the backend, which will begin pulling and processing the MJPEG stream. The live, annotated video will appear in the dashboard via the backend's `/video_feed` endpoint.

## Folder Structure
- `/Guardian`: Contains the Arduino `.ino` sketches for the ESP32 boards.
- `/server`: Contains the Python FastAPI backend and the HTML/CSS/JS frontend.
- `/old_reference`: Contains the original, un-integrated sensor test codes.
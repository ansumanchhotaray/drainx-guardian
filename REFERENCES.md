# References & Citations — DrainX Guardian

This document lists the libraries, frameworks, research papers, hardware documentation, tutorials, and prototype references used to design and implement the **DrainX Guardian** codebase (`Guardian/`, `server/`, and `old_reference/`).

---

## 1. AI Computer Vision & Object Detection (`server/vision.py`, `server/webcam_demo.py`)

### Models & Research Papers
1. **YOLO-World (Zero-Shot Open-Vocabulary Object Detection)**
   - **Usage in Code**: Used in `server/vision.py` (`yolov8s-worldv2.pt`) via `model.set_classes(DRAINAGE_CLASSES)` to detect 12 drainage litter classes in real time using natural-language prompts without training a custom model.
   - **Paper**: Tianheng Cheng, Lin Song, Yixiao Ge, Wenyu Liu, Xinggang Wang, and Ying Shan, *"YOLO-World: Real-Time Open-Vocabulary Object Detection,"* IEEE/CVF Conference on Computer Vision and Pattern Recognition (CVPR), 2024.
   - **Links**:
     - [arXiv Paper (2401.17270)](https://arxiv.org/abs/2401.17270)
     - [Official YOLO-World GitHub Repository (AILab-CVC)](https://github.com/AILab-CVC/YOLO-World)
     - [Ultralytics YOLO-World Documentation](https://docs.ultralytics.com/models/yolo-world/)

2. **TACO Dataset (Trash Annotations in Context)**
   - **Usage in Code**: Referenced in `server/vision.py` (`DRAINAGE_CLASSES` and `CLASS_TO_CATEGORY`) as the taxonomy basis for classifying drainage debris (`plastic bottle cap`, `metal bottle cap`, `plastic wrapper`, `plastic bag`, `six pack rings`, `paper bag`, `scrap paper`, `cardboard`, `leaves`, `cigarette`, `drink can`, `plastic bottle`) into `Plastic`, `Metal`, `Paper`, `Bio`, and `Other`.
   - **Paper**: Pedro F. Proença and Pedro Simões, *"TACO: Trash Annotations in Context for Litter Detection,"* arXiv preprint arXiv:2003.06975, 2020.
   - **Links**:
     - [TACO Dataset Official Website](http://tacodataset.org/)
     - [TACO GitHub Repository](https://github.com/pedropro/TACO)
     - [arXiv Paper (2003.06975)](https://arxiv.org/abs/2003.06975)

3. **OpenAI CLIP (Contrastive Language–Image Pre-training)**
   - **Usage in Code**: Configured in `server/vision.py` (`_load_model_worker()` and `ultralytics.nn.text_model`) to encode the text class labels into embeddings for YOLO-Worldv2 zero-shot inference.
   - **Links**:
     - [OpenAI CLIP GitHub Repository](https://github.com/openai/CLIP)
     - [CLIP Paper (arXiv:2103.00020)](https://arxiv.org/abs/2103.00020)

### Vision Libraries
4. **Ultralytics Python SDK (`ultralytics`)**
   - **Usage in Code**: Model loading (`from ultralytics import YOLO, settings`), cache directory management (`WEIGHTS_DIR`, `RUNS_DIR`), and bounding-box inference (`results.boxes.xyxy`, `cls`, `conf`) in `server/vision.py`.
   - **Links**:
     - [Ultralytics Documentation](https://docs.ultralytics.com/)
     - [Ultralytics GitHub Repository](https://github.com/ultralytics/ultralytics)

5. **OpenCV (`opencv-python` / `cv2`) & NumPy (`numpy`)**
   - **Usage in Code**: Decoding raw JPEG snapshots (`cv2.imdecode(np.frombuffer(...), cv2.IMREAD_COLOR)`) in `server/vision.py`, and local camera capture/display (`cv2.VideoCapture`, `cv2.putText`, `cv2.imshow`) in `server/webcam_demo.py`.
   - **Links**:
     - [OpenCV Official Documentation](https://docs.opencv.org/)
     - [opencv-python PyPI / GitHub](https://github.com/opencv/opencv-python)
     - [NumPy Documentation](https://numpy.org/doc/)

---

## 2. Backend Server & Networking (`server/app.py`, `server/requirements.txt`)

1. **FastAPI (`fastapi`)**
   - **Usage in Code**: Powers the backend REST API (`/api/nodes`, `/api/camera`, `/api/motor`, `/api/arm`, `/api/auto`), static file serving (`StaticFiles`, `FileResponse`), `CORSMiddleware`, real-time WebSocket broadcasting (`ConnectionManager` pattern on `/ws`), and MJPEG video streaming (`StreamingResponse` on `/video_feed`).
   - **Links**:
     - [FastAPI Official Documentation](https://fastapi.tiangolo.com/)
     - [FastAPI WebSockets & ConnectionManager Pattern](https://fastapi.tiangolo.com/advanced/websockets/)
     - [FastAPI `StreamingResponse` Guide](https://fastapi.tiangolo.com/advanced/custom-response/#streamingresponse)

2. **Uvicorn (`uvicorn[standard]`)**
   - **Usage in Code**: ASGI server used in `server/app.py` (`uvicorn.run(app, host="0.0.0.0", port=8000)`) to run FastAPI and WebSockets asynchronously.
   - **Links**:
     - [Uvicorn Official Documentation](https://www.uvicorn.org/)
     - [Uvicorn GitHub Repository](https://github.com/encode/uvicorn)

3. **HTTPX (`httpx`) & Requests (`requests`)**
   - **Usage in Code**:
     - `httpx.AsyncClient` is used in `server/app.py` (`poll_sensors()`, `dispatch_motor_command()`, `dispatch_arm_command()`) for non-blocking asynchronous HTTP communication with ESP32 nodes.
     - `requests.get()` is used in `server/vision.py` (`_poll_snapshots()`) to fetch JPEG frames from the ESP32-CAM `/capture` endpoint and validate JPEG SOI (`0xFFD8`) and EOI (`0xFFD9`) markers.
   - **Links**:
     - [HTTPX Official Documentation](https://www.python-httpx.org/)
     - [Requests Official Documentation](https://requests.readthedocs.io/)

4. **Python Standard Library (`asyncio`, `socket`, `threading`)**
   - **Usage in Code**:
     - `socket` (`AF_INET`, `SOCK_DGRAM`, `SO_BROADCAST`, `SO_REUSEADDR`) in `udp_discovery_worker()` listens on UDP port `5005` for JSON broadcast beacons from the ESP32 nodes.
     - `asyncio.run_coroutine_threadsafe()`, `asyncio.Lock()`, and `asyncio.create_task()` coordinate background telemetry polling, actuator command coalescing, and the autonomous garbage-collection state machine.
   - **Links**:
     - [Python `asyncio` Documentation](https://docs.python.org/3/library/asyncio.html)
     - [Python `socket` Low-Level Networking Interface](https://docs.python.org/3/library/socket.html)

---

## 3. ESP32 & Arduino Firmware (`Guardian/` & `old_reference/`)

### Core Frameworks & Drivers
1. **Espressif Arduino-ESP32 Core (`WiFi.h`, `WebServer.h`, `WiFiUdp.h`, LEDC, FreeRTOS)**
   - **Usage in Code**:
     - Used across `CameraAndArmNode.ino`, `MotorNode.ino`, and `SensorNode.ino` for Wi-Fi station mode (`WiFi.mode(WIFI_STA)`, `WiFi.setSleep(false)`, `WiFi.setTxPower()`), HTTP servers (`WiFiServer`, `WebServer`), and UDP broadcast discovery (`WiFiUDP`).
     - Supports both Arduino-ESP32 v2.x (`ledcSetup` / `ledcAttachPin`) and v3.x (`ledcAttach` / `ledcAttachChannel`) via `#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3` guards in `CameraAndArmNode.ino` and `MotorNode.ino`.
     - Uses FreeRTOS `xTaskCreatePinnedToCore()` to pin the HTTP server task to Core 0 on the ESP32-CAM.
     - Disables the hardware brownout reset register via `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` (`soc/soc.h`, `soc/rtc_cntl_reg.h`).
   - **Links**:
     - [Arduino-ESP32 Official Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
     - [Arduino-ESP32 GitHub Repository](https://github.com/espressif/arduino-esp32)
     - [ESP32 Arduino 2.x to 3.0 Migration Guide (LEDC PWM API)](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html#ledc)
     - [ESP-IDF ADC Oneshot Mode & ADC2 Wi-Fi Conflict Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc_oneshot.html)

2. **Espressif `esp32-camera` Driver (`esp_camera.h`) & AI-Thinker ESP32-CAM**
   - **Usage in Code**: Used in `Guardian/CameraAndArmNode/CameraAndArmNode.ino` and `old_reference/Camera/Camera.ino` (header included in `old_reference/Camera/esp_camera.h`) to initialize the OV2640 sensor (`camera_config_t`, `FRAMESIZE_QVGA`, `PIXFORMAT_JPEG`, `CAMERA_GRAB_LATEST`, PSRAM framebuffers) and capture JPEG snapshots (`esp_camera_fb_get()`, `esp_camera_fb_return()`).
   - **Links**:
     - [Espressif `esp32-camera` Driver Repository](https://github.com/espressif/esp32-camera)
     - [Arduino-ESP32 `CameraWebServer` Official Example](https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/Camera/CameraWebServer)
     - [AI-Thinker ESP32-CAM Pinout & Streaming Guide (Random Nerd Tutorials)](https://randomnerdtutorials.com/esp32-cam-video-streaming-face-recognition-arduino-ide/)
     - [ESP32-CAM AI-Thinker Pinout Guide (Random Nerd Tutorials)](https://randomnerdtutorials.com/esp32-cam-ai-thinker-pinout/)

### Sensors, Actuators & Tutorials
3. **Arduino Servo `Sweep` Example (Robotic Collector Arm)**
   - **Usage in Code**: Included directly in `old_reference/servo-arm/servo-arm.ino` (by BARRAGAN, modified by Scott Fitzgerald) and referenced in `Guardian/CameraAndArmNode/CameraAndArmNode.ino` (`updateServoSmooth()`), where its incremental stepping pattern is adapted to non-blocking 50 Hz LEDC Timer 2 microsecond pulse interpolation (`540 µs` to `2300 µs` on `GPIO 13` / `GPIO 14`) to reduce servo inrush current on the ESP32-CAM 5V rail.
   - **Links**:
     - [Arduino Official `Sweep` Tutorial](https://docs.arduino.cc/learn/electronics/servo-motors/)
     - [Arduino Servo Library Reference & Examples](https://github.com/arduino-libraries/Servo)

4. **Analog pH Sensor Algorithm (`Guardian/SensorNode/SensorNode.ino`, `old_reference/pH/pH_code2.ino`)**
   - **Usage in Code**: `readPH()` implements the standard 10-sample analog buffer (`buffer_arr[10]`), bubble-sort outlier rejection (discarding the 2 lowest and 2 highest readings and averaging the middle 6 samples), 12-bit ESP32 ADC conversion (`(float)avgval * 3.3 / 4095.0 / 6`), and linear Nernst slope conversion (`ph_act = -5.70 * volt + ph_calibration_value`).
   - **Links**:
     - [CircuitDigest — Arduino pH Meter Tutorial (Source of `buffer_arr[10]`, Bubble Sort, and `-5.70 * volt + calibration_value`)](https://circuitdigest.com/microcontroller-projects/arduino-ph-meter)
     - [DFRobot Gravity Analog pH Sensor / Meter Kit Wiki (SEN0161)](https://wiki.dfrobot.com/PH_meter_SKU__SEN0161_)
     - [Electronic Clinic — ESP32 pH Sensor Interfacing & Calibration](https://www.electroniclinic.com/esp32-ph-sensor-interfacing-ph-meter-using-esp32-and-arduino-ide/)

5. **Analog Turbidity Sensor (`Guardian/SensorNode/SensorNode.ino`, `old_reference/Turbidity/Turbidity.ino`)**
   - **Usage in Code**: `readTurbidity()` reads the analog turbidity sensor on `GPIO 32` (`ADC1_CH4`), applies the linear calibration curve `(turbidityValue - 2717.6) / -18.189`, and classifies water into `Clear Water` (`0–49 NTU`), `Turbid` (`50–75 NTU`), and `Very Turbid` (`76–150 NTU`).
   - **Links**:
     - [DFRobot Gravity Analog Turbidity Sensor Wiki (SEN0189)](https://wiki.dfrobot.com/Turbidity_sensor_SKU__SEN0189)
     - [How2Electronics — Interfacing Turbidity Sensor with Arduino/ESP32](https://how2electronics.com/interfacing-turbidity-sensor-with-arduino/)

6. **DS18B20 1-Wire Digital Temperature Sensor (`Guardian/SensorNode/SensorNode.ino`)**
   - **Usage in Code**: Uses `OneWire.h` and `DallasTemperature.h` (`sensors.requestTemperatures()`, `sensors.getTempCByIndex(0)`) on `GPIO 4`.
   - **Links**:
     - [Paul Stoffregen `OneWire` Library (GitHub)](https://github.com/PaulStoffregen/OneWire)
     - [Miles Burton `DallasTemperature` Arduino Library (GitHub)](https://github.com/milesburton/Arduino-Temperature-Control-Library)
     - [Random Nerd Tutorials — ESP32 with DS18B20 Temperature Sensor (Arduino IDE)](https://randomnerdtutorials.com/esp32-ds18b20-temperature-arduino-ide/)

7. **L298N Dual H-Bridge Motor Driver (`Guardian/MotorNode/MotorNode.ino`, `old_reference/Motors/motors.ino`)**
   - **Usage in Code**: Controls two DC motor channels (`IN1`/`IN2` on `GPIO 14`/`27`, `IN3`/`IN4` on `GPIO 26`/`18`/`25`) with 20 kHz, 8-bit PWM speed control (`ENA` on `GPIO 33`, `ENB` on `GPIO 32`).
   - **Links**:
     - [STMicroelectronics L298 Dual Full-Bridge Driver Datasheet (PDF)](https://www.st.com/resource/en/datasheet/l298.pdf)
     - [Random Nerd Tutorials — ESP32 DC Motor and L298N Motor Driver Control](https://randomnerdtutorials.com/esp32-dc-motor-l298n-motor-driver-control-speed-direction/)

---

## 4. Legacy & Prototype Libraries in `old_reference/`

The `/old_reference` directory preserves the standalone test sketches used during hardware prototyping before integrating into the unified FastAPI + UDP Auto-Discovery architecture:

1. **HC-SR04 Ultrasonic Distance Sensor (`old_reference/Ultrasonic/Ultrasonic.ino`, `old_reference/Motors/motors.ino`)**
   - **Usage in Code**: 10 µs `TRIG_PIN` pulse and `pulseIn(ECHO_PIN, HIGH)` time-of-flight calculation (`distance = duration * 0.0343 / 2.0`).
   - **Link**: [Random Nerd Tutorials — ESP32 with HC-SR04 Ultrasonic Sensor](https://randomnerdtutorials.com/esp32-hc-sr04-ultrasonic-arduino/)

2. **Blynk IoT Library (`old_reference/pH/pH_code1.ino`, `old_reference/pH/pH_code2.ino`, `old_reference/Turbidity/Turbidity.ino`)**
   - **Usage in Code**: Early standalone sensor prototypes used `<BlynkSimpleEsp32.h>` and `BlynkTimer` to stream readings to Blynk virtual pins before migrating to the local FastAPI server.
   - **Links**:
     - [Blynk C++ Library (GitHub)](https://github.com/blynkkk/blynk-library)
     - [Blynk Official Documentation](https://docs.blynk.io/)

3. **PubSubClient MQTT Library (`old_reference/Camera/Camera.ino`)**
   - **Usage in Code**: Early camera node prototype used `<PubSubClient.h>` to publish status and telemetry to an MQTT broker on port `1883` before being replaced by UDP broadcast beacons on port `5005`.
   - **Link**: [Nick O'Leary `pubsubclient` Library (GitHub)](https://github.com/knolleary/pubsubclient)

4. **LiquidCrystal I2C Library (`old_reference/Turbidity/Turbidity.ino`)**
   - **Usage in Code**: Drives a 16×2 I2C character LCD (`LiquidCrystal_I2C lcd(0x27, 16, 2)`) in the standalone turbidity sketch.
   - **Link**: [LiquidCrystal_I2C Arduino Library (GitHub)](https://github.com/johnrickman/LiquidCrystal_I2C)

5. **GeoLinker GPS Tracker Library (`old_reference/GPS/GPS.ino`)**
   - **Usage in Code**: Standalone NMEA GPS tracking sketch authored by CircuitDigest / Rithik Krisna M using `<GeoLinker.h>` over `HardwareSerial(1)` (`GPIO 16`/`17`).
   - **Links**:
     - [CircuitDigest GeoLinker Arduino Library (GitHub)](https://github.com/Circuit-Digest/GeoLinker)
     - [CircuitDigest ESP32 GPS Tracker Documentation](https://circuitdigest.com/)

---

## 5. Frontend Dashboard (`server/static/index.html`, `script.js`, `style.css`)

1. **MDN Web APIs (Vanilla JavaScript & CSS3)**
   - **WebSocket API**: Real-time full-duplex telemetry, node discovery status, and vision bounding-box synchronization (`/ws`) — [MDN WebSocket Reference](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket)
   - **Pointer Events API**: Hold-to-drive directional motor buttons using `pointerdown`, `pointerup`, `pointercancel`, and `setPointerCapture()` — [MDN Pointer Events Reference](https://developer.mozilla.org/en-US/docs/Web/API/Pointer_events)
   - **Fetch API**: Asynchronous JSON POST requests to `/api/motor`, `/api/arm`, `/api/auto`, and manual IP override endpoints — [MDN Fetch API Reference](https://developer.mozilla.org/en-US/docs/Web/API/Fetch_API)
   - **CSS Glassmorphism (`backdrop-filter`)**: Frosted-glass UI cards using `backdrop-filter: blur(16px)` — [MDN `backdrop-filter` Reference](https://developer.mozilla.org/en-US/docs/Web/CSS/backdrop-filter)

2. **Google Fonts (`Inter`)**
   - **Usage in Code**: Loaded in `server/static/index.html` for dashboard typography.
   - **Link**: [Google Fonts — Inter](https://fonts.google.com/specimen/Inter)

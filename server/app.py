from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, StreamingResponse
from fastapi.middleware.cors import CORSMiddleware
from typing import List
import vision
import asyncio
import time

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

latest_sensor_data = {
    "turbidity": 0.0,
    "ph": 0.0,
    "water_level_cm": 0.0,
    "temperature": 0.0
}

class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)
        await websocket.send_json(latest_sensor_data)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        dead = []
        for connection in self.active_connections:
            try:
                await connection.send_json(message)
            except Exception as e:
                print(f"WebSocket send error: {e}")
                dead.append(connection)
        for conn in dead:
            self.active_connections.remove(conn)

manager = ConnectionManager()

# --- Static files & root ---
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def get_index():
    return FileResponse("static/index.html")

# --- Set the ESP32-CAM snapshot URL ---
@app.post("/api/camera")
async def set_camera(request: Request):
    """
    Called by the frontend when the user enters the ESP32-CAM IP.
    Body: { "url": "http://192.168.x.x/capture" }
    """
    body = await request.json()
    url = body.get("url", "").strip()
    if not url:
        return {"status": "error", "message": "url is required"}
    vision.set_camera_url(url)
    return {"status": "ok", "url": url}

# --- Processed MJPEG video feed (annotated with garbage detection boxes) ---
async def _frame_generator():
    """Async generator that yields MJPEG boundary-delimited frames."""
    boundary = b"--frame\r\n"
    header = b"Content-Type: image/jpeg\r\n\r\n"
    while True:
        frame = vision.get_latest_frame()
        if frame:
            yield boundary + header + frame + b"\r\n"
        await asyncio.sleep(0.04)  # ~25 fps max

@app.get("/video_feed")
async def video_feed():
    return StreamingResponse(
        _frame_generator(),
        media_type="multipart/x-mixed-replace; boundary=frame"
    )

# --- Sensor data endpoint (called by ESP32 WROOM every 2 seconds) ---
@app.post("/api/sensors")
async def receive_sensor_data(request: Request):
    global latest_sensor_data
    try:
        data = await request.json()
        latest_sensor_data.update(data)
        print(f"[ESP32] {latest_sensor_data}")
        await manager.broadcast(latest_sensor_data)
        return {"status": "ok"}
    except Exception as e:
        print(f"[ERROR] Failed to parse sensor data: {e}")
        return {"status": "error", "message": str(e)}

# --- WebSocket endpoint (used by the browser dashboard) ---
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)

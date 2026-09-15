from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware
from typing import List

app = FastAPI()

# Allow all origins — needed so the browser dashboard can call back to the server
# and so the ESP32 (a different IP on the hotspot) can POST without CORS blocks.
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
        # Send the latest cached data immediately on connection
        await websocket.send_json(latest_sensor_data)

    def disconnect(self, websocket: WebSocket):
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
            await websocket.receive_text()  # Keep connection alive
    except WebSocketDisconnect:
        manager.disconnect(websocket)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)

import json
import asyncio
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from typing import List

app = FastAPI()

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
        # Send latest data immediately upon connection
        await websocket.send_json(latest_sensor_data)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        for connection in self.active_connections:
            try:
                await connection.send_json(message)
            except Exception as e:
                print(f"Error sending to websocket: {e}")

manager = ConnectionManager()

# --- FastAPI Routes ---

# Mount static files (CSS, JS, Images)
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def get_index():
    return FileResponse("static/index.html")

# NEW: Direct HTTP endpoint for the ESP32 to send data to
@app.post("/api/sensors")
async def receive_sensor_data(request: Request):
    global latest_sensor_data
    try:
        data = await request.json()
        latest_sensor_data.update(data)
        print(f"Received data from ESP32: {latest_sensor_data}")
        
        # Broadcast the new data to the frontend dashboard
        await manager.broadcast(latest_sensor_data)
        return {"status": "success"}
    except Exception as e:
        print(f"Error processing sensor data: {e}")
        return {"status": "error", "message": str(e)}

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            # Keep connection alive
            data = await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)

if __name__ == "__main__":
    import uvicorn
    # Run the server on all network interfaces so the ESP32 can reach it
    uvicorn.run(app, host="0.0.0.0", port=8000)

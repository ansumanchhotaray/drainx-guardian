import json
import asyncio
import paho.mqtt.client as mqtt
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from typing import List

app = FastAPI()

# --- MQTT Setup ---
MQTT_BROKER = "127.0.0.1" # Assuming the broker is running on the same laptop
MQTT_PORT = 1883
MQTT_TOPIC = "esp32/sensors"

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

def on_connect(client, userdata, flags, rc):
    print(f"Connected to MQTT Broker with result code {rc}")
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    global latest_sensor_data
    try:
        payload = msg.payload.decode()
        data = json.loads(payload)
        latest_sensor_data.update(data)
        print(f"Received MQTT data: {latest_sensor_data}")
        
        # Broadcast to all connected websockets
        # We need to run this in the asyncio event loop
        try:
            loop = asyncio.get_running_loop()
            loop.create_task(manager.broadcast(latest_sensor_data))
        except RuntimeError:
            pass # No running loop, probably during startup
    except Exception as e:
        print(f"Error processing MQTT message: {e}")

mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

# Start MQTT loop in background
mqtt_client.connect_async(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

# --- FastAPI Routes ---

# Mount static files (CSS, JS, Images)
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def get_index():
    return FileResponse("static/index.html")

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
    uvicorn.run(app, host="0.0.0.0", port=8000)

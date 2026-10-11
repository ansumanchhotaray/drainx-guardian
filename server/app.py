from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, StreamingResponse
from fastapi.middleware.cors import CORSMiddleware
from typing import List, Dict, Optional
import vision
import asyncio
import socket
import threading
import json
import time

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

latest_sensor_data = {
    "turbidity": 0,
    "turbidity_status": "Unknown",
    "turbidity_class": "status-normal",
    "ph": 0.0,
    "temperature": 0.0
}

discovered_nodes: Dict[str, Optional[dict]] = {
    "camera": None,
    "sensor": None
}

class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)
        # Send initial state including sensor data and discovered nodes
        await websocket.send_json({
            "type": "initial_state",
            "sensor_data": latest_sensor_data,
            "nodes": discovered_nodes
        })

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

# --- Node Discovery & Status ---
@app.get("/api/nodes")
async def get_nodes():
    """Return currently discovered ESP32 nodes and their MAC addresses."""
    return discovered_nodes

async def handle_discovered_node(msg: dict, sender_ip: str):
    global _sensor_url
    role = msg.get("role")
    if role not in ("camera", "sensor"):
        return

    mac = str(msg.get("mac", "UNKNOWN")).upper()
    port = int(msg.get("port", 80))
    node_ip = msg.get("ip") or sender_ip

    prev_node = discovered_nodes.get(role)
    is_new = (
        prev_node is None or
        prev_node.get("ip") != node_ip or
        prev_node.get("status") != "online" or
        prev_node.get("mac") != mac
    )

    url = f"http://{node_ip}:{port}/capture" if role == "camera" else f"http://{node_ip}:{port}/sensors"

    discovered_nodes[role] = {
        "role": role,
        "ip": node_ip,
        "mac": mac,
        "port": port,
        "last_seen": time.time(),
        "status": "online",
        "url": url
    }

    if is_new:
        print(f"[Discovery] Detected {role.upper()} Node: IP={node_ip}, MAC={mac}")
        if role == "camera":
            vision.set_camera_url(url)
        elif role == "sensor":
            _sensor_url = url
            print(f"[Discovery] Sensor polling URL updated to: {_sensor_url}")

        await manager.broadcast({
            "type": "node_update",
            "role": role,
            "node": discovered_nodes[role],
            "nodes": discovered_nodes
        })

def udp_discovery_worker(loop: asyncio.AbstractEventLoop):
    """Background worker that listens for UDP discovery beacons from ESP32 nodes."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    try:
        sock.bind(("", 5005))
        print("[Discovery] UDP beacon listener active on port 5005 (0.0.0.0:5005)")
    except Exception as e:
        print(f"[Discovery] Failed to bind UDP port 5005: {e}")
        return

    while True:
        try:
            data, addr = sock.recvfrom(2048)
            text = data.decode("utf-8", errors="ignore").strip()
            if not text:
                continue
            msg = json.loads(text)
            if msg.get("device") == "drainx-guardian":
                asyncio.run_coroutine_threadsafe(
                    handle_discovered_node(msg, addr[0]),
                    loop
                )
        except Exception:
            time.sleep(0.05)

async def monitor_node_liveness():
    """Periodically marks nodes offline if no beacons are received within 8 seconds."""
    while True:
        await asyncio.sleep(4)
        now = time.time()
        changed = False
        for role, node in list(discovered_nodes.items()):
            if node and node.get("status") == "online":
                if now - node.get("last_seen", 0) > 8:
                    node["status"] = "offline"
                    changed = True
                    print(f"[Discovery] {role.upper()} node (MAC: {node.get('mac')}) timed out (offline)")
        if changed:
            await manager.broadcast({
                "type": "node_update",
                "nodes": discovered_nodes
            })

# --- Fallback HTTP Registration Endpoint ---
@app.post("/api/register")
async def register_node(request: Request):
    """Fallback endpoint in case UDP broadcast is blocked by network isolation."""
    body = await request.json()
    client_ip = request.client.host
    await handle_discovered_node(body, client_ip)
    return {"status": "ok", "nodes": discovered_nodes}

# --- Set the ESP32-CAM snapshot URL (Manual Override) ---
@app.post("/api/camera")
async def set_camera(request: Request):
    """
    Called by the frontend for manual ESP32-CAM IP override.
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
        await asyncio.sleep(0.1)  # ~10-25 fps max

@app.get("/video_feed")
async def video_feed():
    return StreamingResponse(
        _frame_generator(),
        media_type="multipart/x-mixed-replace; boundary=frame"
    )

# --- Sensor polling background task ---
_sensor_url = None
_sensor_task = None

async def poll_sensors():
    global latest_sensor_data, _sensor_url
    import httpx
    
    async with httpx.AsyncClient() as client:
        while True:
            if _sensor_url:
                try:
                    resp = await client.get(_sensor_url, timeout=3.0)
                    if resp.status_code == 200:
                        data = resp.json()
                        latest_sensor_data.update(data)
                        print(f"[ESP32 Sensor] {latest_sensor_data}")
                        await manager.broadcast({
                            "type": "sensor_data",
                            **latest_sensor_data
                        })
                except Exception as e:
                    print(f"[Sensor Poll Error] {e}")
            await asyncio.sleep(2)

@app.on_event("startup")
async def startup_event():
    global _sensor_task
    loop = asyncio.get_running_loop()
    # Start UDP discovery beacon listener
    t = threading.Thread(target=udp_discovery_worker, args=(loop,), daemon=True)
    t.start()
    # Start background polling and liveness monitors
    _sensor_task = asyncio.create_task(poll_sensors())
    asyncio.create_task(monitor_node_liveness())

# --- Set the ESP32 Sensor Node URL (Manual Override) ---
@app.post("/api/set_sensor_node")
async def set_sensor_node(request: Request):
    """
    Called by the frontend for manual ESP32 Sensor Node IP override.
    """
    global _sensor_url
    body = await request.json()
    url = body.get("url", "").strip()
    if not url:
        return {"status": "error", "message": "url is required"}
    
    _sensor_url = url
    print(f"[Server] Sensor URL updated to: {_sensor_url}")
    return {"status": "ok", "url": url}

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

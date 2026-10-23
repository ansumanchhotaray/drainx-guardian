from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, StreamingResponse
from fastapi.middleware.cors import CORSMiddleware
from typing import List, Dict, Optional
import vision
import arm_serial
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
    "sensor": None,
    "motor": None,
    "arm": None
}

# Actuator node URLs -- set on discovery or manual override
_motor_url: Optional[str] = None
_arm_url: Optional[str] = None
_event_loop: Optional[asyncio.AbstractEventLoop] = None

def on_arm_serial_status(port: str, angle: int, status: str):
    """Callback triggered by ArmSerialManager when Arduino Uno reports status or disconnects."""
    global _event_loop
    discovered_nodes["arm"] = {
        "role": "arm",
        "ip": "USB",
        "mac": f"PORT:{port}" if port else "PORT:Auto",
        "port": port,
        "last_seen": time.time(),
        "status": status,
        "angle": angle,
        "connection": "serial"
    }
    if _event_loop and _event_loop.is_running():
        asyncio.run_coroutine_threadsafe(
            manager.broadcast({
                "type": "node_update",
                "role": "arm",
                "node": discovered_nodes["arm"],
                "nodes": discovered_nodes,
                "angle": angle
            }),
            _event_loop
        )

arm_manager = arm_serial.ArmSerialManager(on_status_callback=on_arm_serial_status)

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

@app.middleware("http")
async def add_no_cache_header(request: Request, call_next):
    response = await call_next(request)
    if request.url.path.startswith("/static") or request.url.path == "/" or request.url.path.startswith("/api"):
        response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate, max-age=0"
        response.headers["Pragma"] = "no-cache"
        response.headers["Expires"] = "0"
    return response

@app.get("/")
async def get_index():
    return FileResponse(
        "static/index.html",
        headers={
            "Cache-Control": "no-cache, no-store, must-revalidate, max-age=0",
            "Pragma": "no-cache",
            "Expires": "0"
        }
    )

# --- Node Discovery & Status ---
@app.get("/api/nodes")
async def get_nodes():
    """Return currently discovered ESP32 nodes, MAC addresses, and AI vision detection status."""
    return {**discovered_nodes, "vision": vision.get_detection_state()}

async def handle_discovered_node(msg: dict, sender_ip: str):
    global _sensor_url, _motor_url, _arm_url
    role = msg.get("role")
    if role not in ("camera", "sensor", "motor", "arm"):
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

    if role == "camera":
        url = f"http://{node_ip}:{port}/capture"
    elif role == "sensor":
        url = f"http://{node_ip}:{port}/sensors"
    elif role == "motor":
        url = f"http://{node_ip}:{port}/move"
    else:  # arm (network fallback)
        url = f"http://{node_ip}:{port}/arm"

    node_data = {
        "role": role,
        "ip": node_ip,
        "mac": mac,
        "port": port,
        "last_seen": time.time(),
        "status": "online",
        "url": url
    }
    if role == "arm":
        node_data["angle"] = int(msg.get("angle", 90))
        node_data["connection"] = "network"

    discovered_nodes[role] = node_data

    if is_new:
        print(f"[Discovery] Detected {role.upper()} Node: IP={node_ip}, MAC={mac}")
        if role == "camera":
            vision.set_camera_url(url)
        elif role == "sensor":
            _sensor_url = url
            print(f"[Discovery] Sensor polling URL updated to: {_sensor_url}")
        elif role == "motor":
            _motor_url = url
            print(f"[Discovery] Motor node URL updated to: {_motor_url}")
        elif role == "arm":
            _arm_url = url
            print(f"[Discovery] Arm node URL updated to: {_arm_url}")

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
    """Periodically marks nodes offline if no beacons/heartbeats are received."""
    while True:
        await asyncio.sleep(4)
        now = time.time()
        changed = False
        for role, node in list(discovered_nodes.items()):
            if node and node.get("status") == "online":
                if role == "arm" and node.get("connection") == "serial":
                    if not arm_manager.is_online or (now - node.get("last_seen", 0) > 10):
                        node["status"] = "offline"
                        changed = True
                elif now - node.get("last_seen", 0) > 18:
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

async def monitor_vision_status():
    """Broadcasts AI debris detection and bounding box updates to dashboard in real time."""
    last_state = None
    while True:
        await asyncio.sleep(0.1)
        state = vision.get_detection_state()
        if state != last_state:
            last_state = state
            await manager.broadcast({
                "type": "vision_update",
                "vision": state
            })

@app.on_event("startup")
async def startup_event():
    global _sensor_task, _event_loop
    loop = asyncio.get_running_loop()
    _event_loop = loop
    # Pre-warm AI model in background
    vision.start_model_loading()
    # Start Arm USB Serial auto-detect manager
    arm_manager.start()
    # Start UDP discovery beacon listener
    t = threading.Thread(target=udp_discovery_worker, args=(loop,), daemon=True)
    t.start()
    # Start background polling and liveness monitors
    _sensor_task = asyncio.create_task(poll_sensors())
    asyncio.create_task(monitor_node_liveness())
    asyncio.create_task(monitor_vision_status())

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

# --- Motor Node Control ---
_motor_lock = asyncio.Lock()
_pending_motor_params: Optional[dict] = None

@app.post("/api/motor")
async def motor_command(request: Request):
    """
    Send a movement command to the Motor Node.
    Body: { "dir": "f"|"b"|"l"|"r"|"s", "dur": <ms optional>, "speed": <100-255 optional> }
    """
    global _motor_url, _pending_motor_params
    import httpx
    body = await request.json()
    dir_cmd = body.get("dir", "s")
    dur     = body.get("dur", 0)
    speed   = body.get("speed", None)

    if not _motor_url:
        # Check if motor node is in discovered_nodes but _motor_url not yet set
        motor_node = discovered_nodes.get("motor")
        if motor_node and motor_node.get("status") == "online":
            _motor_url = motor_node["url"]
        else:
            return {"status": "error", "message": "Motor node not discovered yet"}

    params = {"dir": dir_cmd, "dur": dur}
    if speed:
        params["speed"] = speed

    # Coalesce rapid commands so only the latest command is sent next
    _pending_motor_params = params

    if _motor_lock.locked():
        return {"status": "queued", "dir": dir_cmd}

    async with _motor_lock:
        last_resp_text = ""
        while _pending_motor_params is not None:
            current_params = _pending_motor_params
            _pending_motor_params = None
            try:
                async with httpx.AsyncClient() as client:
                    resp = await client.get(
                        _motor_url,
                        params=current_params,
                        headers={"Connection": "close"},
                        timeout=1.5
                    )
                    last_resp_text = resp.text
            except Exception as e:
                return {"status": "error", "message": str(e)}
        return {"status": "ok", "dir": dir_cmd, "motor_response": last_resp_text}

@app.post("/api/motor_speed")
async def motor_speed_command(request: Request):
    """Update the drive speed on the Motor Node."""
    global _motor_url
    import httpx
    body = await request.json()
    speed = body.get("speed", 150)

    if not _motor_url:
        motor_node = discovered_nodes.get("motor")
        if motor_node and motor_node.get("status") == "online":
            _motor_url = motor_node["url"]
        else:
            return {"status": "error", "message": "Motor node not discovered yet"}

    speed_url = _motor_url.rsplit("/", 1)[0] + "/speed"
    async with _motor_lock:
        try:
            async with httpx.AsyncClient() as client:
                resp = await client.get(
                    speed_url,
                    params={"val": speed},
                    headers={"Connection": "close"},
                    timeout=1.5
                )
                return {"status": "ok", "speed": resp.text}
        except Exception as e:
            return {"status": "error", "message": str(e)}

@app.post("/api/set_motor_node")
async def set_motor_node(request: Request):
    """Manual IP override for the Motor Node."""
    global _motor_url
    body = await request.json()
    ip = body.get("ip", "").strip()
    if not ip:
        return {"status": "error", "message": "ip is required"}
    _motor_url = f"http://{ip}/move"
    print(f"[Server] Motor URL manually set to: {_motor_url}")
    return {"status": "ok", "url": _motor_url}

# --- Arm Node Control (Robotic Collector Arm - Arduino Uno) ---
@app.post("/api/arm")
async def arm_command(request: Request):
    """
    Control the robotic arm servo angle (0 - 180 degrees).
    Body: { "angle": 90 }
    """
    global _arm_url
    body = await request.json()
    angle = int(body.get("angle", 90))
    angle = max(0, min(180, angle))

    dispatched = False
    # 1. Try USB Serial connection to Arduino Uno
    if arm_manager.is_online or (arm_manager.conn and arm_manager.conn.is_open):
        dispatched = arm_manager.set_angle(angle)

    # 2. Try HTTP network fallback if available
    if not dispatched and _arm_url:
        import httpx
        try:
            async with httpx.AsyncClient() as client:
                resp = await client.post(_arm_url, json={"angle": angle}, timeout=2.0)
                dispatched = (resp.status_code == 200)
        except Exception as e:
            print(f"[Arm HTTP Error] {e}")

    # Update state
    if discovered_nodes.get("arm"):
        discovered_nodes["arm"]["angle"] = angle
        discovered_nodes["arm"]["last_seen"] = time.time()
    else:
        discovered_nodes["arm"] = {
            "role": "arm",
            "ip": "USB" if arm_manager.port else "Manual",
            "mac": f"PORT:{arm_manager.port or 'Auto'}",
            "port": arm_manager.port or 80,
            "last_seen": time.time(),
            "status": "online" if dispatched else "offline",
            "angle": angle,
            "connection": "serial" if arm_manager.port else "network"
        }

    # Broadcast arm angle update over WebSocket
    await manager.broadcast({
        "type": "arm_update",
        "angle": angle,
        "nodes": discovered_nodes
    })

    return {"status": "ok", "angle": angle, "dispatched": dispatched}

@app.get("/api/arm")
async def get_arm_state():
    """Return current angle, connection status, and port of the Arm Node."""
    return {
        "status": "ok",
        "angle": arm_manager.current_angle,
        "connected": arm_manager.is_online,
        "port": arm_manager.port,
        "node": discovered_nodes.get("arm")
    }

@app.get("/api/arm/ports")
async def get_arm_ports():
    """List available serial COM ports for the Arduino Uno Arm Node."""
    return {
        "status": "ok",
        "ports": arm_manager.list_ports()
    }

@app.post("/api/set_arm_node")
async def set_arm_node(request: Request):
    """
    Manual port or IP override for Arm Node.
    Body: { "port": "COM4" } or { "url": "http://192.168.137.x/arm" }
    """
    global _arm_url
    body = await request.json()
    port = body.get("port", "").strip()
    url = body.get("url", "").strip()

    if port:
        ok = arm_manager.connect(port, manual=True)
        if ok:
            discovered_nodes["arm"] = {
                "role": "arm",
                "ip": "USB",
                "mac": f"PORT:{port}",
                "port": port,
                "last_seen": time.time(),
                "status": "online",
                "angle": arm_manager.current_angle,
                "connection": "serial"
            }
            await manager.broadcast({
                "type": "node_update",
                "role": "arm",
                "node": discovered_nodes["arm"],
                "nodes": discovered_nodes
            })
        return {"status": "ok" if ok else "error", "port": port, "connected": ok}
    elif url:
        _arm_url = url
        discovered_nodes["arm"] = {
            "role": "arm",
            "ip": url,
            "mac": "MANUAL",
            "port": 80,
            "last_seen": time.time(),
            "status": "online",
            "angle": 90,
            "url": url,
            "connection": "network"
        }
        await manager.broadcast({
            "type": "node_update",
            "role": "arm",
            "node": discovered_nodes["arm"],
            "nodes": discovered_nodes
        })
        return {"status": "ok", "url": url}
    else:
        return {"status": "error", "message": "port or url is required"}

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

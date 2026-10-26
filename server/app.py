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
    "sensor": None,
    "motor": None,
    "arm": None
}

# Actuator node URLs & state -- set on discovery or manual override
_motor_url: Optional[str] = None
_arm_url: Optional[str] = None
_arm_angle: int = 90
_motor_dir: str = "s"
_motor_speed: int = 175

# Autonomous Garbage Collection state
_auto_mode: bool = False
_auto_state: str = "idle"  # "idle" | "cruising" | "collecting"
_last_debris_seen_at: float = 0.0
_auto_last_cmd_dir: Optional[str] = None
_auto_last_cmd_angle: Optional[int] = None
_auto_motor_synced: bool = False
_auto_arm_synced: bool = False
_auto_last_retry_at: float = 0.0
AUTO_PICKUP_HOLD_SEC: float = 2.5


def get_auto_state() -> dict:
    """Return current Autonomous Garbage Collection mode status and actuator telemetry."""
    v_state = vision.get_detection_state()
    return {
        "enabled": _auto_mode,
        "state": _auto_state if _auto_mode else "idle",
        "motor_dir": _motor_dir,
        "motor_speed": _motor_speed,
        "arm_angle": _arm_angle,
        "debris_detected": bool(v_state.get("debris_detected", False)),
        "items": v_state.get("items", [])
    }

class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)
        # Send initial state including sensor data, discovered nodes, and auto state
        await websocket.send_json({
            "type": "initial_state",
            "sensor_data": latest_sensor_data,
            "nodes": discovered_nodes,
            "auto": get_auto_state()
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
    """Return currently discovered ESP32 nodes, MAC addresses, AI vision detection status, and auto state."""
    return {
        **discovered_nodes,
        "vision": vision.get_detection_state(),
        "auto": get_auto_state()
    }

async def handle_discovered_node(msg: dict, sender_ip: str):
    global _sensor_url, _motor_url, _arm_url, _arm_angle, _auto_motor_synced, _auto_arm_synced
    role = msg.get("role")
    if role not in ("camera", "camera_arm", "sensor", "motor", "arm"):
        return

    mac = str(msg.get("mac", "UNKNOWN")).upper()
    port = int(msg.get("port", 80))
    node_ip = msg.get("ip") or sender_ip

    # CameraAndArmNode combines both camera (/capture) and arm (/arm) on the same ESP32-CAM
    if role in ("camera", "camera_arm"):
        prev_cam = discovered_nodes.get("camera")
        prev_arm = discovered_nodes.get("arm")
        is_new = (
            prev_cam is None or
            prev_cam.get("ip") != node_ip or
            prev_cam.get("status") != "online" or
            prev_cam.get("mac") != mac or
            prev_arm is None or
            prev_arm.get("status") != "online"
        )

        cam_url = f"http://{node_ip}:{port}/capture"
        arm_url = f"http://{node_ip}:{port}/arm"
        if "angle" in msg and not _auto_mode:
            _arm_angle = int(msg["angle"])

        now = time.time()
        discovered_nodes["camera"] = {
            "role": "camera",
            "ip": node_ip,
            "mac": mac,
            "port": port,
            "last_seen": now,
            "status": "online",
            "url": cam_url
        }
        discovered_nodes["arm"] = {
            "role": "arm",
            "ip": node_ip,
            "mac": mac,
            "port": port,
            "last_seen": now,
            "status": "online",
            "angle": _arm_angle,
            "url": arm_url
        }
        _arm_url = arm_url

        if is_new:
            print(f"[Discovery] Detected CAMERA+ARM Node: IP={node_ip}, MAC={mac}")
            vision.set_camera_url(cam_url)
            if _auto_mode:
                _auto_arm_synced = False
            await manager.broadcast({
                "type": "node_update",
                "role": "camera_arm",
                "node": discovered_nodes["camera"],
                "nodes": discovered_nodes,
                "auto": get_auto_state()
            })
        return

    prev_node = discovered_nodes.get(role)
    is_new = (
        prev_node is None or
        prev_node.get("ip") != node_ip or
        prev_node.get("status") != "online" or
        prev_node.get("mac") != mac
    )

    if role == "sensor":
        url = f"http://{node_ip}:{port}/sensors"
    elif role == "motor":
        url = f"http://{node_ip}:{port}/move"
    else:  # standalone arm
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
        if "angle" in msg and not _auto_mode:
            _arm_angle = int(msg["angle"])
        node_data["angle"] = _arm_angle

    discovered_nodes[role] = node_data

    if is_new:
        print(f"[Discovery] Detected {role.upper()} Node: IP={node_ip}, MAC={mac}")
        if role == "sensor":
            _sensor_url = url
            print(f"[Discovery] Sensor polling URL updated to: {_sensor_url}")
        elif role == "motor":
            _motor_url = url
            if _auto_mode:
                _auto_motor_synced = False
            print(f"[Discovery] Motor node URL updated to: {_motor_url}")
        elif role == "arm":
            _arm_url = url
            if _auto_mode:
                _auto_arm_synced = False
            print(f"[Discovery] Arm node URL updated to: {_arm_url}")

        await manager.broadcast({
            "type": "node_update",
            "role": role,
            "node": discovered_nodes[role],
            "nodes": discovered_nodes,
            "auto": get_auto_state()
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
    """Periodically marks nodes offline if no beacons are received within 18 seconds."""
    while True:
        await asyncio.sleep(4)
        now = time.time()
        changed = False
        for role, node in list(discovered_nodes.items()):
            if node and node.get("status") == "online":
                if now - node.get("last_seen", 0) > 18:
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
    global _arm_url
    body = await request.json()
    url = body.get("url", "").strip()
    if not url:
        return {"status": "error", "message": "url is required"}
    vision.set_camera_url(url)
    base_url = url.rsplit("/", 1)[0]
    _arm_url = f"{base_url}/arm"
    return {"status": "ok", "url": url}

# --- Processed MJPEG video feed ---
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
    """Broadcasts AI debris detection, bounding boxes, and drives Autonomous Garbage Collection."""
    global _auto_state, _last_debris_seen_at, _auto_last_cmd_dir, _auto_last_cmd_angle
    global _auto_motor_synced, _auto_arm_synced, _auto_last_retry_at
    last_state = None
    while True:
        await asyncio.sleep(0.1)
        state = vision.get_detection_state()
        if state != last_state:
            last_state = state
            await manager.broadcast({
                "type": "vision_update",
                "vision": state,
                "auto": get_auto_state()
            })

        if _auto_mode:
            now = time.time()
            debris_detected = bool(state.get("debris_detected", False))
            if debris_detected:
                _last_debris_seen_at = now

            # Once garbage is detected in Auto Mode, latch into 'collecting' state (wheels stopped, arm at 180°)
            # so the arm stays raised holding the garbage and never drops back to 90° if the camera loses sight
            # of the lifted garbage or experiences a frame hiccup.
            in_pickup_cycle = (_auto_state == "collecting") or debris_detected

            if in_pickup_cycle:
                new_auto_state = "collecting"
                target_dir = "s"
                target_angle = 180
            else:
                new_auto_state = "cruising"
                target_dir = "f"
                target_angle = 90

            state_changed = (new_auto_state != _auto_state)
            _auto_state = new_auto_state

            motor_online = bool(discovered_nodes.get("motor") and discovered_nodes["motor"].get("status") == "online")
            arm_online = bool(discovered_nodes.get("arm") and discovered_nodes["arm"].get("status") == "online")

            dir_changed = (_auto_last_cmd_dir != target_dir) or (_motor_dir != target_dir)
            angle_changed = (_auto_last_cmd_angle != target_angle) or (_arm_angle != target_angle)
            retry_due = (now - _auto_last_retry_at) >= 2.0

            need_motor = dir_changed or (motor_online and not _auto_motor_synced and retry_due)
            need_arm = angle_changed or (arm_online and not _auto_arm_synced and retry_due)

            if need_motor or need_arm:
                _auto_last_retry_at = now

                if target_dir == "s":
                    # Stop wheel motors immediately when garbage is detected, then raise arm to 180°
                    if need_motor:
                        _auto_last_cmd_dir = target_dir
                        print("[AutoMode] Garbage detected! Stopping wheel motors (dir=s)")
                        res = await dispatch_motor_command("s", speed=_motor_speed)
                        _auto_motor_synced = (res.get("status") in ("ok", "queued"))
                    if need_arm:
                        _auto_last_cmd_angle = target_angle
                        print(f"[AutoMode] Raising collector arm to {target_angle}° to pick up garbage")
                        res = await dispatch_arm_command(target_angle)
                        _auto_arm_synced = (res.get("status") in ("ok", "queued"))
                else:
                    # Path is clear: keep arm at default 90° and drive wheels forward
                    if need_arm:
                        _auto_last_cmd_angle = target_angle
                        print(f"[AutoMode] Setting collector arm to default {target_angle}°")
                        res = await dispatch_arm_command(target_angle)
                        _auto_arm_synced = (res.get("status") in ("ok", "queued"))
                    if need_motor:
                        _auto_last_cmd_dir = target_dir
                        print(f"[AutoMode] Path clear — moving wheels forward (dir=f, speed={_motor_speed})")
                        res = await dispatch_motor_command("f", speed=_motor_speed)
                        _auto_motor_synced = (res.get("status") in ("ok", "queued"))

            if state_changed:
                await manager.broadcast({
                    "type": "auto_update",
                    "auto": get_auto_state()
                })

@app.on_event("startup")
async def startup_event():
    global _sensor_task
    loop = asyncio.get_running_loop()
    # Pre-warm AI model in background
    vision.start_model_loading()
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


async def dispatch_motor_command(dir_cmd: str, dur: int = 0, speed: Optional[int] = None) -> dict:
    """Internal helper to send movement commands to MotorNode and broadcast state."""
    global _motor_url, _pending_motor_params, _motor_dir, _motor_speed
    import httpx

    _motor_dir = dir_cmd
    if speed is not None:
        _motor_speed = int(speed)

    # Broadcast wheel motor state immediately for responsive dashboard UI
    await manager.broadcast({
        "type": "motor_update",
        "dir": _motor_dir,
        "speed": _motor_speed,
        "auto": get_auto_state()
    })

    if not _motor_url:
        motor_node = discovered_nodes.get("motor")
        if motor_node and motor_node.get("status") == "online":
            _motor_url = motor_node["url"]
        else:
            return {"status": "error", "message": "Motor node not discovered yet", "dir": dir_cmd}

    params = {"dir": dir_cmd, "dur": dur}
    if speed is not None:
        params["speed"] = int(speed)

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
                return {"status": "error", "message": str(e), "dir": dir_cmd}
        return {"status": "ok", "dir": dir_cmd, "motor_response": last_resp_text}


@app.post("/api/motor")
async def motor_command(request: Request):
    """
    Send a movement command to the Motor Node.
    Body: { "dir": "f"|"b"|"l"|"r"|"s", "dur": <ms optional>, "speed": <100-255 optional> }
    """
    body = await request.json()
    dir_cmd = body.get("dir", "s")
    dur     = body.get("dur", 0)
    speed   = body.get("speed", None)

    # If Autonomous Mode is active and garbage is detected, prevent wheels from driving into garbage
    if _auto_mode and _auto_state == "collecting" and dir_cmd != "s":
        dir_cmd = "s"

    return await dispatch_motor_command(dir_cmd, dur=dur, speed=speed)


@app.post("/api/motor_speed")
async def motor_speed_command(request: Request):
    """Update the drive speed on the Motor Node."""
    global _motor_url, _motor_speed
    import httpx
    body = await request.json()
    speed = int(body.get("speed", 175))
    _motor_speed = speed

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
    global _motor_url, _auto_motor_synced
    body = await request.json()
    ip = body.get("ip", "").strip()
    if not ip:
        return {"status": "error", "message": "ip is required"}
    _motor_url = f"http://{ip}/move"
    if _auto_mode:
        _auto_motor_synced = False
    print(f"[Server] Motor URL manually set to: {_motor_url}")
    return {"status": "ok", "url": _motor_url}


# --- Arm Node Control (Robotic Collector Arm on CameraAndArmNode) ---
_arm_lock = asyncio.Lock()
_pending_arm_angle: Optional[int] = None


async def dispatch_arm_command(angle: int) -> dict:
    """Internal helper to send servo angle commands (0-180°) to CameraAndArmNode and broadcast state."""
    global _arm_url, _arm_angle, _pending_arm_angle
    import httpx

    angle = max(0, min(180, int(angle)))
    angle_delta = abs(angle - _arm_angle)
    _arm_angle = angle

    if not _arm_url:
        arm_node = discovered_nodes.get("arm")
        if arm_node and arm_node.get("url"):
            _arm_url = arm_node["url"]
        elif discovered_nodes.get("camera") and discovered_nodes["camera"].get("ip"):
            cam = discovered_nodes["camera"]
            _arm_url = f"http://{cam['ip']}:{cam.get('port', 80)}/arm"

    if discovered_nodes.get("arm"):
        discovered_nodes["arm"]["angle"] = angle

    # Broadcast arm angle update over WebSocket immediately for responsive UI
    await manager.broadcast({
        "type": "arm_update",
        "angle": angle,
        "nodes": discovered_nodes,
        "auto": get_auto_state()
    })

    if not _arm_url:
        return {"status": "error", "message": "Arm node not discovered yet", "angle": angle}

    # Pause camera snapshot polling while the servo moves so ESP32-CAM doesn't suffer
    # simultaneous camera DMA + Wi-Fi JPEG TX + servo current spikes
    pause_sec = max(1.2, min(3.0, (angle_delta / 45.0) + 0.8))
    vision.pause_for_arm_movement(pause_sec)

    # Coalesce rapid slider movements so only the latest angle is sent next
    _pending_arm_angle = angle
    if _arm_lock.locked():
        return {"status": "queued", "angle": angle}

    async with _arm_lock:
        # Allow any in-flight /capture request on the single-threaded ESP32-CAM to finish
        await asyncio.sleep(0.08)
        last_resp_text = ""
        while _pending_arm_angle is not None:
            target_angle = _pending_arm_angle
            _pending_arm_angle = None
            try:
                async with httpx.AsyncClient() as client:
                    resp = await client.get(
                        _arm_url,
                        params={"angle": target_angle},
                        headers={"Connection": "close"},
                        timeout=2.5
                    )
                    last_resp_text = resp.text
            except Exception as e:
                return {"status": "error", "message": str(e), "angle": angle}
        return {"status": "ok", "angle": angle, "arm_response": last_resp_text}


@app.post("/api/arm")
async def arm_command(request: Request):
    """
    Send a servo angle command (0 - 180 degrees) to CameraAndArmNode over LAN.
    Body: { "angle": 90 }
    """
    body = await request.json()
    angle = int(body.get("angle", 90))
    return await dispatch_arm_command(angle)


@app.get("/api/arm")
async def get_arm_state():
    """Return current angle and LAN node status of the Arm Node."""
    arm_node = discovered_nodes.get("arm")
    return {
        "status": "ok",
        "angle": _arm_angle,
        "connected": bool(arm_node and arm_node.get("status") == "online"),
        "node": arm_node
    }


@app.post("/api/set_arm_node")
async def set_arm_node(request: Request):
    """
    Manual IP override for Arm Node (CameraAndArmNode).
    Body: { "ip": "192.168.137.x" }
    """
    global _arm_url, _auto_arm_synced
    body = await request.json()
    ip = (body.get("ip") or body.get("port") or "").strip()
    if not ip:
        return {"status": "error", "message": "ip is required"}

    if ip.startswith("http://") or ip.startswith("https://"):
        _arm_url = ip if ip.endswith("/arm") else f"{ip.rstrip('/')}/arm"
        clean_ip = ip.split("//", 1)[1].split("/")[0].split(":")[0]
    else:
        clean_ip = ip
        _arm_url = f"http://{clean_ip}/arm"

    discovered_nodes["arm"] = {
        "role": "arm",
        "ip": clean_ip,
        "mac": "MANUAL",
        "port": 80,
        "last_seen": time.time(),
        "status": "online",
        "angle": _arm_angle,
        "url": _arm_url
    }
    if _auto_mode:
        _auto_arm_synced = False
    print(f"[Server] Arm URL manually set to: {_arm_url}")
    await manager.broadcast({
        "type": "node_update",
        "role": "arm",
        "node": discovered_nodes["arm"],
        "nodes": discovered_nodes,
        "auto": get_auto_state()
    })
    return {"status": "ok", "url": _arm_url}


# --- Autonomous Garbage Collection Mode Endpoints ---
@app.get("/api/auto")
async def get_auto_mode():
    """Get current Autonomous Garbage Collection mode state."""
    return {"status": "ok", "auto": get_auto_state()}


@app.post("/api/auto")
async def set_auto_mode(request: Request):
    """
    Enable or disable Autonomous Garbage Collection mode.
    Body: { "enabled": true | false }
    When enabled:
      - If no garbage is detected: arm stays at default 90°, wheels move forward ('f').
      - If garbage is detected by camera: wheels immediately stop ('s'), arm moves from 90° to 180°.
    When disabled:
      - Wheels stop ('s') and arm returns to default 90°.
    """
    global _auto_mode, _auto_state, _last_debris_seen_at
    global _auto_last_cmd_dir, _auto_last_cmd_angle, _auto_motor_synced, _auto_arm_synced, _auto_last_retry_at

    try:
        body = await request.json()
    except Exception:
        body = {}

    if "enabled" in body:
        enabled = bool(body["enabled"])
    else:
        enabled = not _auto_mode

    _auto_mode = enabled
    _auto_last_retry_at = time.time()

    if _auto_mode:
        debris_now = bool(vision.get_detection_state().get("debris_detected", False))
        if debris_now:
            _last_debris_seen_at = time.time()
            _auto_state = "collecting"
            _auto_last_cmd_dir = "s"
            _auto_last_cmd_angle = 180
            print("[AutoMode] Enabled -> Garbage currently detected! Stopping wheels & moving arm to 180°")
            m_res = await dispatch_motor_command("s", speed=_motor_speed)
            a_res = await dispatch_arm_command(180)
            _auto_motor_synced = (m_res.get("status") in ("ok", "queued"))
            _auto_arm_synced = (a_res.get("status") in ("ok", "queued"))
        else:
            _last_debris_seen_at = 0.0
            _auto_state = "cruising"
            _auto_last_cmd_dir = "f"
            _auto_last_cmd_angle = 90
            print("[AutoMode] Enabled -> Path clear! Setting arm to 90° & driving wheels forward")
            a_res = await dispatch_arm_command(90)
            m_res = await dispatch_motor_command("f", speed=_motor_speed)
            _auto_arm_synced = (a_res.get("status") in ("ok", "queued"))
            _auto_motor_synced = (m_res.get("status") in ("ok", "queued"))
    else:
        _auto_state = "idle"
        _last_debris_seen_at = 0.0
        _auto_last_cmd_dir = None
        _auto_last_cmd_angle = None
        _auto_motor_synced = False
        _auto_arm_synced = False
        print("[AutoMode] Disabled -> Stopping wheels & returning arm to default 90°")
        await dispatch_motor_command("s", speed=_motor_speed)
        await dispatch_arm_command(90)

    auto_payload = get_auto_state()
    await manager.broadcast({
        "type": "auto_update",
        "auto": auto_payload
    })
    return {"status": "ok", "auto": auto_payload}

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

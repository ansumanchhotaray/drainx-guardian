import json
import threading
import time
import re
import serial
import serial.tools.list_ports

class ArmSerialManager:
    """
    Manages USB Serial communication with the Arduino Uno ArmNode.
    Handles auto-discovery across COM ports, background reading of heartbeats,
    and dispatching angle positioning commands.
    """
    def __init__(self, on_status_callback=None):
        self.on_status_callback = on_status_callback
        self.port: str | None = None
        self.baud: int = 115200
        self.conn: serial.Serial | None = None
        self.current_angle: int = 90
        self.is_online: bool = False
        self.last_seen: float = 0
        
        self._lock = threading.Lock()
        self._running = False
        self._reader_thread: threading.Thread | None = None
        self._auto_thread: threading.Thread | None = None
        self._manual_override_port: str | None = None

    def start(self):
        """Start auto-detection background task."""
        self._running = True
        self._auto_thread = threading.Thread(target=self._auto_detect_worker, daemon=True)
        self._auto_thread.start()

    def stop(self):
        self._running = False
        self.disconnect()

    def list_ports(self) -> list[dict]:
        """List all available serial COM ports."""
        ports = []
        for p in serial.tools.list_ports.comports():
            ports.append({
                "port": p.device,
                "description": p.description,
                "hwid": p.hwid
            })
        return ports

    def connect(self, port_name: str, baud: int = 115200, manual: bool = True) -> bool:
        """Connect to a specific serial port."""
        with self._lock:
            if self.conn and self.conn.is_open:
                try:
                    self.conn.close()
                except Exception:
                    pass
                self.conn = None

            if manual:
                self._manual_override_port = port_name

            try:
                print(f"[ArmNode] Attempting connection to {port_name} @ {baud} baud...")
                conn = serial.Serial(
                    port=port_name,
                    baudrate=baud,
                    timeout=1.0,
                    write_timeout=1.0
                )
                self.conn = conn
                self.port = port_name
                self.baud = baud
                self.is_online = True
                self.last_seen = time.time()
                print(f"[ArmNode] Connected to Arduino Uno on {port_name}")

                # Start reader thread if not active
                self._reader_thread = threading.Thread(target=self._read_loop, daemon=True)
                self._reader_thread.start()

                # Handshake / ping
                time.sleep(0.1)
                self.conn.write(b"PING\n")
                self.conn.flush()
                return True
            except Exception as e:
                print(f"[ArmNode] Failed to connect to {port_name}: {e}")
                self.is_online = False
                self.conn = None
                return False

    def disconnect(self):
        """Close connection."""
        with self._lock:
            if self.conn:
                try:
                    self.conn.close()
                except Exception:
                    pass
                self.conn = None
            self.is_online = False
            self.port = None

    def set_angle(self, angle: int) -> bool:
        """Send target servo angle (0 - 180 degrees) to Arduino Uno."""
        angle = max(0, min(180, int(angle)))
        self.current_angle = angle

        with self._lock:
            if not self.conn or not self.conn.is_open:
                print(f"[ArmNode] Cannot send angle {angle}: Serial not connected")
                return False

            try:
                cmd = f"A:{angle}\n".encode("utf-8")
                self.conn.write(cmd)
                self.conn.flush()
                print(f"[ArmNode] Sent command: A:{angle}")
                return True
            except Exception as e:
                print(f"[ArmNode] Error sending angle command: {e}")
                self.is_online = False
                return False

    def _read_loop(self):
        """Background loop reading JSON status and responses from Arduino Uno."""
        while self._running and self.conn and self.conn.is_open:
            try:
                line = self.conn.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    continue

                # Try parsing as JSON heartbeat or response
                if line.startswith("{") and line.endswith("}"):
                    try:
                        data = json.loads(line)
                        if data.get("device") == "drainx-guardian" and data.get("role") == "arm":
                            self.is_online = True
                            self.last_seen = time.time()
                            if "angle" in data:
                                self.current_angle = int(data["angle"])
                            if self.on_status_callback:
                                self.on_status_callback(self.port, self.current_angle, "online")
                    except Exception:
                        pass
                else:
                    # Check for direct angle reports, e.g. "A:90"
                    match = re.search(r"A:?(\d+)", line, re.IGNORECASE)
                    if match:
                        self.current_angle = int(match.group(1))
                        if self.on_status_callback:
                            self.on_status_callback(self.port, self.current_angle, "online")
            except Exception as e:
                if self._running:
                    print(f"[ArmNode] Serial read error on {self.port}: {e}")
                    self.is_online = False
                    if self.on_status_callback:
                        self.on_status_callback(self.port, self.current_angle, "offline")
                break

    def _auto_detect_worker(self):
        """Continuously probes available COM ports if no connection is active."""
        while self._running:
            if not self.conn or not self.conn.is_open or not self.is_online:
                # If a manual port was selected, prioritize it
                if self._manual_override_port:
                    success = self.connect(self._manual_override_port, manual=False)
                    if success:
                        time.sleep(3)
                        continue

                # Otherwise scan available ports
                ports = serial.tools.list_ports.comports()
                for p in ports:
                    desc = (p.description or "").lower()
                    # Skip bluetooth serial links to prevent long timeout freezes
                    if "bluetooth" in desc:
                        continue

                    # Look for Arduino, CH340, USB Serial, Genuino
                    is_candidate = any(k in desc for k in ("arduino", "ch340", "usb", "genuino", "serial"))
                    if is_candidate:
                        try:
                            test_conn = serial.Serial(p.device, 115200, timeout=0.8, write_timeout=0.8)
                            time.sleep(1.5)  # Arduino resets on DTR open
                            test_conn.write(b"PING\n")
                            test_conn.flush()
                            resp = test_conn.readline().decode("utf-8", errors="ignore")
                            test_conn.close()

                            if "drainx-guardian" in resp or "arm" in resp.lower():
                                print(f"[ArmNode] Auto-detected ArmNode on {p.device} ({p.description})")
                                self.connect(p.device, manual=False)
                                break
                        except Exception:
                            pass
            time.sleep(3)

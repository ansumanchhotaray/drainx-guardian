import cv2
import threading
import numpy as np
import requests
import time

# --- YOLO-World Zero-Shot Classes ---
# Since you don't want to train a custom TACO model, we can use YOLO-World!
# It allows us to search for exact English terms without any training.
DRAINAGE_CLASSES = [
    "plastic bottle cap",  # 0
    "metal bottle cap",    # 1
    "plastic wrapper",     # 2
    "plastic bag",         # 3
    "six pack rings",      # 4
    "paper bag",           # 5
    "scrap paper",         # 6
    "cardboard",           # 7
    "leaves",              # 8 (Natural litter)
    "cigarette",           # 9
    "drink can",           # 10
    "plastic bottle"       # 11
]

# Map the classes to our UI categories and CSS colors
CLASS_TO_CATEGORY = {
    "plastic bottle cap": ("Plastic", "#ef4444"),
    "metal bottle cap": ("Metal", "#f59e0b"),
    "plastic wrapper": ("Plastic", "#ef4444"),
    "plastic bag": ("Plastic", "#ef4444"),
    "six pack rings": ("Plastic", "#ef4444"),
    "paper bag": ("Paper", "#0284c7"),
    "scrap paper": ("Paper", "#0284c7"),
    "cardboard": ("Paper", "#0284c7"),
    "leaves": ("Bio", "#10b981"),
    "cigarette": ("Other", "#8b5cf6"),
    "drink can": ("Metal", "#f59e0b"),
    "plastic bottle": ("Plastic", "#ef4444")
}

_model = None
_model_loading = False
_model_lock = threading.Lock()

def _load_model_worker():
    global _model, _model_loading
    try:
        from ultralytics import YOLO
        print("[Vision] Loading YOLO-World (Zero-Shot) in background...")
        model = YOLO("yolov8s-worldv2.pt")
        model.set_classes(DRAINAGE_CLASSES)
        with _model_lock:
            _model = model
        print("[Vision] YOLO-World ready with TACO-like classes!")
    except Exception as e:
        print(f"[Vision] Error loading YOLO model: {e}")
    finally:
        _model_loading = False

def start_model_loading():
    """Start loading the AI model in background without blocking video streaming."""
    global _model_loading
    if _model is None and not _model_loading:
        _model_loading = True
        t = threading.Thread(target=_load_model_worker, daemon=True)
        t.start()

def get_model():
    return _model

# --- Shared state ---
_camera_url: str = None
_latest_frame: bytes = None
_frame_lock = threading.Lock()
_running = False
_thread: threading.Thread = None
_debris_detected: bool = False
_detected_items: list[str] = []
_detected_boxes: list[dict] = []


def get_detection_state() -> dict:
    """Return current garbage/debris detection status and bounding boxes for the web UI."""
    return {
        "debris_detected": _debris_detected,
        "items": list(_detected_items),
        "model_ready": _model is not None,
        "boxes": list(_detected_boxes)
    }


def detect_objects(frame):
    """Run YOLO-World on frame, updating detection state without modifying image pixels."""
    global _model, _debris_detected, _detected_items, _detected_boxes
    if _model is None:
        _debris_detected = False
        _detected_items = []
        _detected_boxes = []
        return

    try:
        results = _model(frame, verbose=False)[0]
        garbage_found = False
        detected_list = []
        boxes_list = []
        h, w = frame.shape[:2]

        for box in results.boxes:
            cls_id = int(box.cls[0])
            conf = float(box.conf[0])

            if conf > 0.15:  # YOLO-World threshold
                garbage_found = True
                x1, y1, x2, y2 = map(int, box.xyxy[0])
                
                obj_name = DRAINAGE_CLASSES[cls_id]
                category, color = CLASS_TO_CATEGORY[obj_name]
                
                label = f"{obj_name} {conf:.0%}"
                detected_list.append(label)

                # Normalized percentages for frontend CSS overlay (0-100%)
                boxes_list.append({
                    "label": obj_name,
                    "category": category,
                    "conf": int(round(conf * 100)),
                    "color": color,
                    "x": max(0.0, round(float(x1 / w * 100), 2)),
                    "y": max(0.0, round(float(y1 / h * 100), 2)),
                    "w": min(100.0, round(float((x2 - x1) / w * 100), 2)),
                    "h": min(100.0, round(float((y2 - y1) / h * 100), 2)),
                })

        _debris_detected = garbage_found
        _detected_items = detected_list
        _detected_boxes = boxes_list
    except Exception as e:
        print(f"[Vision] Inference error: {e}")


def annotate_frame(frame):
    """Backward-compatible wrapper; runs detection without modifying frame pixels."""
    detect_objects(frame)
    return frame


def _poll_snapshots():
    """Background thread: polls ESP32-CAM /capture endpoint, runs YOLO, stores raw pristine frame."""
    global _latest_frame, _running

    # Start model loading in background without blocking the snapshot stream
    start_model_loading()

    while _running:
        url = _camera_url
        if not url:
            time.sleep(0.5)
            continue

        try:
            # Fetch a single JPEG snapshot from the ESP32-CAM
            response = requests.get(url, headers={"Connection": "close"}, timeout=3)

            if response.status_code != 200:
                print(f"[Vision] Snapshot request failed: HTTP {response.status_code}")
                time.sleep(0.2)
                continue

            # Decode the JPEG image for YOLO detection
            jpg_data = response.content
            frame = cv2.imdecode(
                np.frombuffer(jpg_data, dtype=np.uint8),
                cv2.IMREAD_COLOR
            )
            if frame is None:
                print("[Vision] Failed to decode JPEG snapshot")
                time.sleep(0.2)
                continue

            # Run YOLO inference (clean, no image alteration)
            detect_objects(frame)

            # Store the pristine camera JPEG bytes directly (no re-encoding overhead)
            with _frame_lock:
                _latest_frame = jpg_data

        except requests.exceptions.ConnectionError:
            print(f"[Vision] Cannot reach camera at {url}. Retrying in 2s...")
            time.sleep(2)
        except requests.exceptions.Timeout:
            print(f"[Vision] Snapshot request timed out. Retrying...")
            time.sleep(0.5)
        except Exception as exc:
            print(f"[Vision] Error: {exc}. Retrying in 1s...")
            time.sleep(1)


def set_camera_url(url: str):
    """Set (or update) the ESP32-CAM snapshot URL and start the processing thread."""
    global _camera_url, _running, _thread
    _camera_url = url

    if not _running:
        _running = True
        _thread = threading.Thread(target=_poll_snapshots, daemon=True)
        _thread.start()
        print(f"[Vision] Snapshot polling thread started for: {url}")
    else:
        print(f"[Vision] Camera URL updated to: {url}")


def get_latest_frame() -> bytes | None:
    """Return the most recently processed (annotated) JPEG frame, or None."""
    with _frame_lock:
        return _latest_frame

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

# Map the classes to our UI categories and colors
CLASS_TO_CATEGORY = {
    "plastic bottle cap": ("Plastic", (0, 0, 255)),
    "metal bottle cap": ("Metal", (255, 165, 0)),
    "plastic wrapper": ("Plastic", (0, 0, 255)),
    "plastic bag": ("Plastic", (0, 0, 255)),
    "six pack rings": ("Plastic", (0, 0, 255)),
    "paper bag": ("Paper", (0, 255, 255)),
    "scrap paper": ("Paper", (0, 255, 255)),
    "cardboard": ("Paper", (0, 255, 255)),
    "leaves": ("Bio", (0, 255, 0)),
    "cigarette": ("Other", (128, 128, 128)),
    "drink can": ("Metal", (255, 165, 0)),
    "plastic bottle": ("Plastic", (0, 0, 255))
}

_model = None
_model_lock = threading.Lock()

def get_model():
    """Lazy-load the YOLO-World model for zero-shot detection."""
    global _model
    if _model is None:
        with _model_lock:
            if _model is None:
                from ultralytics import YOLO
                print("[Vision] Loading YOLO-World (Zero-Shot)...")
                # yolov8s-worldv2.pt is the default world model, it downloads automatically
                _model = YOLO("yolov8s-worldv2.pt")
                # Set our exact custom text prompts
                _model.set_classes(DRAINAGE_CLASSES)
                print("[Vision] YOLO-World ready with TACO-like classes!")
    return _model


# --- Shared state ---
_camera_url: str = None
_latest_frame: bytes = None
_frame_lock = threading.Lock()
_running = False
_thread: threading.Thread = None


def annotate_frame(frame):
    """Run YOLO-World on a frame and draw bounding boxes."""
    model = get_model()
    results = model(frame, verbose=False)[0]

    garbage_found = False
    for box in results.boxes:
        cls_id = int(box.cls[0])
        conf = float(box.conf[0])

        if conf > 0.15:  # YOLO-World sometimes has lower confidences for niche text
            garbage_found = True
            x1, y1, x2, y2 = map(int, box.xyxy[0])
            
            obj_name = DRAINAGE_CLASSES[cls_id]
            category, color = CLASS_TO_CATEGORY[obj_name]
            
            label = f"{obj_name} {conf:.0%}"

            # Draw bounding box
            cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)

            # Draw label pill background
            (tw, th), _ = cv2.getTextSize(
                label, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 1
            )
            cv2.rectangle(
                frame,
                (x1, y1 - th - 8),
                (x1 + tw + 6, y1),
                color, -1
            )
            
            # Put white text inside the pill
            cv2.putText(
                frame, label,
                (x1 + 3, y1 - 4),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55,
                (255, 255, 255), 1, cv2.LINE_AA
            )

    # Optional: watermark when garbage is detected
    if garbage_found:
        cv2.putText(
            frame, "DEBRIS DETECTED",
            (10, frame.shape[0] - 10),
            cv2.FONT_HERSHEY_SIMPLEX, 0.7,
            (0, 0, 220), 2, cv2.LINE_AA
        )

    return frame


def _poll_snapshots():
    """Background thread: polls ESP32-CAM /capture endpoint, runs YOLO, stores annotated frame."""
    global _latest_frame, _running

    # Pre-load the model so the first frame isn't slow
    get_model()

    while _running:
        url = _camera_url
        if not url:
            time.sleep(1)
            continue

        try:
            # Fetch a single JPEG snapshot from the ESP32-CAM
            response = requests.get(url, timeout=5)

            if response.status_code != 200:
                print(f"[Vision] Snapshot request failed: HTTP {response.status_code}")
                time.sleep(0.5)
                continue

            # Decode the JPEG image
            jpg_data = response.content
            frame = cv2.imdecode(
                np.frombuffer(jpg_data, dtype=np.uint8),
                cv2.IMREAD_COLOR
            )
            if frame is None:
                print("[Vision] Failed to decode JPEG snapshot")
                time.sleep(0.5)
                continue

            # Run YOLO inference and draw bounding boxes
            frame = annotate_frame(frame)

            # Encode the annotated frame back to JPEG
            _, jpeg = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 85])
            with _frame_lock:
                _latest_frame = jpeg.tobytes()

        except requests.exceptions.ConnectionError:
            print(f"[Vision] Cannot reach camera at {url}. Retrying in 3s...")
            time.sleep(3)
        except requests.exceptions.Timeout:
            print(f"[Vision] Snapshot request timed out. Retrying...")
            time.sleep(1)
        except Exception as exc:
            print(f"[Vision] Error: {exc}. Retrying in 2s...")
            time.sleep(2)


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

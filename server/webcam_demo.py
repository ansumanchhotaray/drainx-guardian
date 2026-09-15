"""
DrainX Guardian — Webcam Garbage Detection Demo
================================================
Runs YOLOv8n on your laptop webcam in real-time and draws
a red bounding box around any detected garbage objects.

Run from the server/ directory:
    guix shell python python-opencv python-numpy -- python webcam_demo.py

Or if you have pip:
    pip install ultralytics opencv-python
    python webcam_demo.py

Press Q to quit.
"""

import cv2
from ultralytics import YOLO

# --- Same garbage-relevant COCO classes as the live system ---
GARBAGE_CLASSES = {
    24: "backpack",
    26: "handbag",
    28: "suitcase",
    39: "bottle",
    40: "wine glass",
    41: "cup",
    45: "bowl",
    73: "book",
    75: "vase",
    79: "toothbrush",
}

CONFIDENCE_THRESHOLD = 0.35

def main():
    print("Loading YOLOv8n model (downloads ~6 MB on first run)...")
    model = YOLO("yolov8n.pt")
    print("Model ready. Opening webcam...")

    cap = cv2.VideoCapture(0)  # 0 = default webcam; change to 1/2 for other cameras
    if not cap.isOpened():
        print("ERROR: Could not open webcam. Try changing the index in VideoCapture(0).")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    print("Running! Press Q to quit.")

    while True:
        ret, frame = cap.read()
        if not ret:
            print("Failed to grab frame.")
            break

        # --- Run YOLOv8 inference ---
        results = model(frame, verbose=False)[0]

        garbage_found = False
        for box in results.boxes:
            cls_id = int(box.cls[0])
            conf  = float(box.conf[0])

            if cls_id in GARBAGE_CLASSES and conf > CONFIDENCE_THRESHOLD:
                garbage_found = True
                x1, y1, x2, y2 = map(int, box.xyxy[0])
                label = f"{GARBAGE_CLASSES[cls_id]} {conf:.0%}"

                # Red bounding box
                cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 0, 220), 2)

                # Red filled label background
                (tw, th), _ = cv2.getTextSize(
                    label, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 1
                )
                cv2.rectangle(
                    frame,
                    (x1, y1 - th - 8),
                    (x1 + tw + 6, y1),
                    (0, 0, 220), -1
                )
                cv2.putText(
                    frame, label,
                    (x1 + 3, y1 - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                    (255, 255, 255), 1, cv2.LINE_AA
                )

        # --- Status overlay ---
        status_text = "GARBAGE DETECTED" if garbage_found else "No garbage detected"
        status_color = (0, 0, 220) if garbage_found else (0, 200, 80)
        cv2.putText(
            frame, status_text,
            (10, frame.shape[0] - 12),
            cv2.FONT_HERSHEY_SIMPLEX, 0.65,
            status_color, 2, cv2.LINE_AA
        )

        # --- Watermark ---
        cv2.putText(
            frame, "DrainX Guardian | YOLOv8n Demo",
            (10, 24),
            cv2.FONT_HERSHEY_SIMPLEX, 0.6,
            (200, 200, 200), 1, cv2.LINE_AA
        )

        cv2.imshow("DrainX Guardian — Webcam Demo (Q to quit)", frame)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()
    print("Done.")


if __name__ == "__main__":
    main()

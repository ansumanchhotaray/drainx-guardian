"""
DrainX Guardian — Webcam Garbage Detection Demo
================================================
Runs the YOLO-World vision module on your laptop webcam in real-time
and draws bounding boxes around any detected debris/garbage objects.

Run from the server/ directory:
    guix shell python python-opencv python-numpy -- python webcam_demo.py

Or if you have pip:
    pip install ultralytics opencv-python requests
    python webcam_demo.py

Press Q to quit.
"""

import cv2
import vision

def main():
    print("Loading YOLO-World model via vision module (may take a moment)...")
    
    # Pre-load the model so the webcam doesn't freeze on the first frame
    vision.get_model()
    
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

        # --- Run YOLO-World inference using our shared vision module ---
        # This will draw the bounding boxes, color coding, and labels automatically!
        annotated_frame = vision.annotate_frame(frame)

        # --- Watermark ---
        cv2.putText(
            annotated_frame, "DrainX Guardian | YOLO-World Webcam Demo",
            (10, 24),
            cv2.FONT_HERSHEY_SIMPLEX, 0.6,
            (200, 200, 200), 1, cv2.LINE_AA
        )

        cv2.imshow("DrainX Guardian — Webcam Demo (Q to quit)", annotated_frame)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()
    print("Done.")


if __name__ == "__main__":
    main()

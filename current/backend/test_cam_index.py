#!/usr/bin/env python3
import cv2

def test_path(path: str):
    print(f"\n=== Testing camera path {path} ===")
    cap = cv2.VideoCapture(path, cv2.CAP_V4L2)
    if not cap.isOpened():
        print(f"  [FAIL] Cannot open {path}")
        return
    ret, frame = cap.read()
    if not ret:
        print(f"  [FAIL] Opened but cannot read frame from {path}")
    else:
        h, w = frame.shape[:2]
        print(f"  [OK] Got frame {w}x{h} from {path}")
    cap.release()

def main():
    test_path("/dev/video0")
    test_path("/dev/video1")

if __name__ == "__main__":
    main()


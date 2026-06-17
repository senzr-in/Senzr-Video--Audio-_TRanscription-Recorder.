import threading
import time
from current.backend.camera_detector import PersonDetector

stop = threading.Event()
detector = PersonDetector()

t = threading.Thread(target=detector.run, args=(stop,), daemon=True)
t.start()

print("Smoke test running for 30 seconds.")
print("Step in front of the camera, then step out.")
time.sleep(30)

stop.set()
t.join(timeout=5)
print("Smoke test finished.")

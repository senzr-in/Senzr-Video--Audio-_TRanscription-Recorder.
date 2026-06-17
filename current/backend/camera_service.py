import sys
import os
sys.path.insert(0, "/opt/edge-gateway")

import threading
from current.backend.camera_detector import PersonDetector
from current.backend.s3_uploader import upload_recording

def main():
    stop = threading.Event()
    detector = PersonDetector(s3_uploader=upload_recording)
    detector.run(stop)

if __name__ == "__main__":
    main()

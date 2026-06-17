import threading

from camera_detector import PersonDetector
from s3_uploader import upload_recording


def main():
    stop = threading.Event()

    detector = PersonDetector(
        s3_uploader=upload_recording
    )

    detector.run(stop)


if __name__ == "__main__":
    main()

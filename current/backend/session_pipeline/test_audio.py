import time

from audio_capture import AudioRecorder


def main():
    recorder = AudioRecorder("test.wav")

    print("Recording 10 seconds...")

    recorder.start()

    time.sleep(10)

    recorder.stop()

    print("Finished")


if __name__ == "__main__":
    main()

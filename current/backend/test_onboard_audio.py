import pyaudio
import wave

OUTPUT_WAV = "test_onboard_audio.wav"
SAMPLE_RATE = 16000
CHANNELS = 1          # we’ll just use mono even though the codec reports 2 inputs
CHUNK_SECONDS = 0.5
RECORD_SECONDS = 5
ONBOARD_DEVICE_INDEX = 2  # rockchip,es8388 on-board codec


def main():
    p = pyaudio.PyAudio()

    # Optional: print just the chosen device for confirmation
    info = p.get_device_info_by_host_api_device_index(0, ONBOARD_DEVICE_INDEX)
    print("Using input device:")
    print(f"  index        : {ONBOARD_DEVICE_INDEX}")
    print(f"  name         : {info.get('name')}")
    print(f"  maxInputCh   : {info.get('maxInputChannels')}")
    print(f"  defaultSR    : {info.get('defaultSampleRate')}")

    frames_per_buffer = int(SAMPLE_RATE * CHUNK_SECONDS)

    print(f"\nOpening input stream on device {ONBOARD_DEVICE_INDEX}...")
    stream = p.open(
        format=pyaudio.paInt16,
        channels=CHANNELS,
        rate=SAMPLE_RATE,
        input=True,
        input_device_index=ONBOARD_DEVICE_INDEX,
        frames_per_buffer=frames_per_buffer,
    )

    print(f"[TEST] Recording {RECORD_SECONDS} seconds from on-board mic...")
    frames = []
    total_chunks = int(RECORD_SECONDS / CHUNK_SECONDS)

    for i in range(total_chunks):
        data = stream.read(frames_per_buffer, exception_on_overflow=False)
        frames.append(data)
        print(f"  recorded chunk {i + 1}/{total_chunks}")

    print("[TEST] Stopping stream")
    stream.stop_stream()
    stream.close()
    p.terminate()

    print(f"[TEST] Writing WAV file: {OUTPUT_WAV}")
    wf = wave.open(OUTPUT_WAV, "wb")
    wf.setnchannels(CHANNELS)
    wf.setsampwidth(2)  # 16-bit audio
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(b"".join(frames))
    wf.close()

    print("[TEST] Done. Copy test_onboard_audio.wav to your VM and listen to verify quality.")


if __name__ == "__main__":
    main()
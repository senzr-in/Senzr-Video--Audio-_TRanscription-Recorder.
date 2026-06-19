import subprocess
import logging
import pyaudio
import time
import threading

# Audio
AUDIO_DEVICE_PRIORITY = ["rockchip,es8388", "es8388"]
CHANNELS      = 2
RATE          = 48000
CHUNK_SAMPLES = 1920  # 40ms chunks at 48kHz

def _get_audio_device_index():
    pa = pyaudio.PyAudio()
    alsa_card = None
    best_priority = len(AUDIO_DEVICE_PRIORITY)
    try:
        print("=== PyAudio devices ===")
        for i in range(pa.get_device_count()):
            info = pa.get_device_info_by_index(i)
            name = info.get('name', '')
            print(i, name, "inputs=", info.get('maxInputChannels'))
            lname = name.lower()
            if info.get('maxInputChannels', 0) > 0:
                for p_idx, p in enumerate(AUDIO_DEVICE_PRIORITY):
                    if p in lname and p_idx < best_priority:
                        best_priority = p_idx
                        # parse "(hw:X,0)" from the name
                        if "(hw:" in name:
                            start = name.find("(hw:") + 4
                            end = name.find(",", start)
                            card_str = name[start:end]
                            alsa_card = int(card_str)
                        break
        print("=======================")
    except Exception:
        pass
    pa.terminate()
    return alsa_card

def audio_thread(stop_event, recording_active, safe_push, time_offset_ns=0):
    audio_error = False
    card = _get_audio_device_index()
    if card is None:
        logging.warning("Audio: no matching device, thread exiting.")
        return

    cmd = [
        "arecord",
        "-D", f"hw:{card},0",
        "-t", "raw",
        "-f", "S16_LE",
        "-c", str(CHANNELS),
        "-r", str(RATE),
        "-"
    ]
    chunk_size = CHUNK_SAMPLES * CHANNELS * 2  # 7680 bytes per chunk
    logging.info("Audio: Starting arecord: %s", " ".join(cmd))

    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, bufsize=10**6)

        while not stop_event.is_set():
            try:
                chunk = proc.stdout.read(chunk_size)
                if not chunk:
                    audio_error = True
                    break
                if recording_active and chunk:
                    synced_ns = time.time_ns() + time_offset_ns
                    safe_push("audio", synced_ns, chunk)
            except Exception:
                audio_error = True
                break
        proc.terminate()
        proc.wait()
    except Exception as e:
        logging.error("Audio open error: %s", e)
        audio_error = True

    return audio_error


# ---- simple test harness ----

def safe_push(kind, ts_ns, data):
    # For testing: just print size. In real code you'd push to a queue.
    print(f"{kind} chunk at {ts_ns}, bytes={len(data)}")

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    stop_event = threading.Event()
    recording_active = True  # always record during test

    t = threading.Thread(
        target=audio_thread,
        args=(stop_event, recording_active, safe_push),
        daemon=True,
    )
    t.start()

    try:
        print("Recording from mic for 5 seconds...")
        time.sleep(5)
    finally:
        stop_event.set()
        t.join()
        print("Recording stopped.")

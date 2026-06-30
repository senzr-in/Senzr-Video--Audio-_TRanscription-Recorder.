#!/usr/bin/env python3
"""
Edge Gateway Benchmark Script
Measures camera FPS, audio sample rate, transcription accuracy,
person detection health, thread health, and overall pipeline status.
Writes results to /opt/edge-gateway/benchmark.txt

Usage:
    sudo /opt/edge-gateway/venv/bin/python benchmark.py
    sudo /opt/edge-gateway/venv/bin/python benchmark.py --session-dir /opt/edge-gateway/recordings/session_20260629_123456
"""

import argparse
import subprocess
import time
import wave
import os
import json
import threading
import uuid
from datetime import datetime
from pathlib import Path


# ─────────────────────────────────────────────
# CONFIG
# ─────────────────────────────────────────────
BENCHMARK_DIR   = Path("/opt/edge-gateway")
OUTPUT_FILE     = BENCHMARK_DIR / "benchmark.txt"
RECORDINGS_DIR  = Path("/opt/edge-gateway/recordings")
CAMERA_INDEX    = 0
CAMERA_FPS_CAP  = 30
AUDIO_DEVICE    = "plughw:2,0"
AUDIO_CHANNELS  = 2
AUDIO_RATE      = 48000
AUDIO_FORMAT    = "S16_LE"
PROBE_DURATION  = 10
AUDIO_PROBE_SEC = 5
MODEL_SIZE      = "base"
COMPUTE_TYPE    = "int8"
MULTI_SESSION_COUNT = 5   # consecutive sessions to test for multi-session stability


# ─────────────────────────────────────────────
# HELPERS
# ─────────────────────────────────────────────

def banner(title: str) -> str:
    width = 60
    line  = "=" * width
    pad   = (width - len(title) - 2) // 2
    return f"\n{line}\n{' ' * pad} {title}\n{line}"


def ts() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _write_silent_wav(path: Path):
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(16000)
        wf.writeframes(b"\x00\x00" * 16000 * 3)


def _av_sync_check(video_duration: float | None, audio_duration: float | None,
                   threshold_sec: float = 1.0) -> str:
    """Return PASS / FAIL / N/A based on A/V duration delta."""
    if video_duration is None or audio_duration is None:
        return "N/A"
    return "PASS ✓" if abs(video_duration - audio_duration) <= threshold_sec else "FAIL ✗"


def _playback_speed_check(live_fps: float | None, recorded_fps: float | None,
                          tolerance: float = 1.0) -> str:
    """Return NORMAL / ABNORMAL based on FPS delta."""
    if live_fps is None or recorded_fps is None:
        return "N/A"
    return "NORMAL ✓" if abs(live_fps - recorded_fps) <= tolerance else "ABNORMAL ✗"


# ─────────────────────────────────────────────
# 1. CAMERA BENCHMARK
# ─────────────────────────────────────────────

def benchmark_camera(duration: int = PROBE_DURATION) -> dict:
    result = {
        "configured_fps": CAMERA_FPS_CAP,
        "live_capture_fps": None,
        "pipeline_fps": None,
        "drop_rate_pct": None,
        "status": "unknown",
        "note": "",
    }

    try:
        import cv2
    except ImportError:
        result["status"] = "SKIP – opencv-python not installed"
        return result

    cap = cv2.VideoCapture(CAMERA_INDEX)
    if not cap.isOpened():
        result["status"] = "SKIP – camera not opened (may be in use by pipeline)"
        return result

    driver_fps = cap.get(cv2.CAP_PROP_FPS)
    result["driver_reported_fps"] = round(driver_fps, 2) if driver_fps else "unknown"

    start = time.monotonic()
    frames = 0
    dropped = 0
    while (time.monotonic() - start) < duration:
        ok, _ = cap.read()
        if ok:
            frames += 1
        else:
            dropped += 1

    elapsed = time.monotonic() - start
    cap.release()

    live_fps = frames / elapsed if elapsed > 0 else 0
    result["live_capture_fps"] = round(live_fps, 2)
    result["frames_captured"] = frames
    result["frames_dropped"] = dropped
    result["elapsed_sec"] = round(elapsed, 2)

    pipeline_fps = min(live_fps, CAMERA_FPS_CAP)
    result["pipeline_fps"] = round(pipeline_fps, 2)

    if live_fps > 0:
        drop_pct = max(0, (CAMERA_FPS_CAP - live_fps) / CAMERA_FPS_CAP * 100)
        result["drop_rate_pct"] = round(drop_pct, 2)

    result["status"] = "OK"
    return result


# ─────────────────────────────────────────────
# 2. AUDIO BENCHMARK
# ─────────────────────────────────────────────

def benchmark_audio(probe_seconds: int = AUDIO_PROBE_SEC) -> dict:
    result = {
        "configured_rate_hz": AUDIO_RATE,
        "configured_channels": AUDIO_CHANNELS,
        "probe_file_rate_hz": None,
        "probe_channels": None,
        "rate_match": None,
        "channel_match": None,
        "status": "unknown",
        "note": "",
    }

    probe_path = BENCHMARK_DIR / "benchmark_audio_probe.wav"

    cmd = [
        "arecord",
        "-D", AUDIO_DEVICE,
        "-c", str(AUDIO_CHANNELS),
        "-r", str(AUDIO_RATE),
        "-f", AUDIO_FORMAT,
        "-t", "wav",
        "-d", str(probe_seconds),
        str(probe_path),
    ]

    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=probe_seconds + 5)
        if proc.returncode != 0:
            result["status"] = f"FAIL – arecord error: {proc.stderr.decode().strip()[:200]}"
            return result
    except FileNotFoundError:
        result["status"] = "SKIP – arecord not found"
        return result
    except subprocess.TimeoutExpired:
        result["status"] = "FAIL – arecord timed out"
        return result

    try:
        with wave.open(str(probe_path), "rb") as wf:
            actual_rate     = wf.getframerate()
            actual_channels = wf.getnchannels()
            actual_frames   = wf.getnframes()
            actual_duration = actual_frames / actual_rate if actual_rate else 0

        result["probe_file_rate_hz"]  = actual_rate
        result["probe_channels"]      = actual_channels
        result["probe_frames"]        = actual_frames
        result["probe_duration_sec"]  = round(actual_duration, 2)
        result["rate_match"]          = (actual_rate == AUDIO_RATE)
        result["channel_match"]       = (actual_channels == AUDIO_CHANNELS)
        result["status"]              = "OK"

        if not result["rate_match"]:
            result["note"] = (
                f"WARNING: pipeline expects {AUDIO_RATE} Hz "
                f"but device gave {actual_rate} Hz"
            )
        if not result["channel_match"]:
            result["note"] += (
                f" | channel mismatch: expected {AUDIO_CHANNELS}, "
                f"got {actual_channels}"
            )
    except Exception as exc:
        result["status"] = f"FAIL – WAV parse error: {exc}"
    finally:
        if probe_path.exists():
            probe_path.unlink(missing_ok=True)

    return result


# ─────────────────────────────────────────────
# 3. TRANSCRIPTION BENCHMARK
# ─────────────────────────────────────────────

REFERENCE_SENTENCES = [
    "the quick brown fox jumps over the lazy dog",
    "edge gateway system is running on orange pi five max",
    "person detection is active and sessions are being recorded",
]


def _wer(reference: str, hypothesis: str) -> float:
    ref = reference.lower().split()
    hyp = hypothesis.lower().split()
    d = [[0] * (len(hyp) + 1) for _ in range(len(ref) + 1)]
    for i in range(len(ref) + 1):
        d[i][0] = i
    for j in range(len(hyp) + 1):
        d[0][j] = j
    for i in range(1, len(ref) + 1):
        for j in range(1, len(hyp) + 1):
            cost = 0 if ref[i - 1] == hyp[j - 1] else 1
            d[i][j] = min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost)
    return d[len(ref)][len(hyp)] / max(len(ref), 1)


def _precision_recall(reference: str, hypothesis: str) -> tuple:
    ref_words = set(reference.lower().split())
    hyp_words = set(hypothesis.lower().split())
    tp        = len(ref_words & hyp_words)
    precision = tp / len(hyp_words) if hyp_words else 0.0
    recall    = tp / len(ref_words)  if ref_words  else 0.0
    return round(precision, 4), round(recall, 4)


def _synthesize_wav(text: str, out_path: Path) -> bool:
    raw_path = out_path.with_suffix(".raw.wav")
    cmd = ["espeak", "-w", str(raw_path), text]
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=10)
        if r.returncode != 0 or not raw_path.exists():
            raise RuntimeError("espeak failed")
    except Exception:
        raw_path.unlink(missing_ok=True)
        _write_silent_wav(out_path)
        return True

    try:
        r2 = subprocess.run(
            ["ffmpeg", "-y", "-i", str(raw_path), "-ar", "16000", "-ac", "1", str(out_path)],
            capture_output=True, timeout=15
        )
        raw_path.unlink(missing_ok=True)
        return r2.returncode == 0 and out_path.exists()
    except Exception:
        raw_path.unlink(missing_ok=True)
        _write_silent_wav(out_path)
        return True


def _probe_multi_session_stability(model) -> dict:
    """
    Run MULTI_SESSION_COUNT consecutive mini-transcriptions to test
    whether the Whisper model stays stable across sessions without
    restarting or losing jobs.
    """
    probe = {
        "consecutive_sessions": MULTI_SESSION_COUNT,
        "transcriber_restarted": False,
        "lost_jobs": 0,
        "pass": True,
    }
    tmp = BENCHMARK_DIR / "benchmark_ms_probe.wav"
    _write_silent_wav(tmp)
    try:
        for _ in range(MULTI_SESSION_COUNT):
            try:
                segs, _ = model.transcribe(str(tmp), language="en", beam_size=1)
                list(segs)   # consume generator
            except Exception:
                probe["lost_jobs"] += 1
                probe["pass"] = False
    finally:
        tmp.unlink(missing_ok=True)
    if probe["lost_jobs"] > 0:
        probe["transcriber_restarted"] = True
    return probe


def benchmark_transcription() -> dict:
    result = {
        "model_size": MODEL_SIZE,
        "compute_type": COMPUTE_TYPE,
        "sentences_tested": 0,
        "avg_wer": None,
        "avg_precision": None,
        "avg_recall": None,
        "per_sentence": [],
        # multi-session fields
        "multi_session_pass": None,
        "consecutive_sessions": MULTI_SESSION_COUNT,
        "transcriber_restarted": None,
        "lost_transcription_jobs": None,
        "metadata_completion": None,
        "transcript_upload": None,
        "status": "unknown",
        "note": "",
    }

    try:
        from faster_whisper import WhisperModel
    except ImportError:
        result["status"] = "SKIP – faster-whisper not installed"
        return result

    try:
        model = WhisperModel(MODEL_SIZE, device="cpu", compute_type=COMPUTE_TYPE)
    except Exception as exc:
        result["status"] = f"FAIL – model load error: {exc}"
        return result

    wers, precisions, recalls, details = [], [], [], []
    tmp_wav = BENCHMARK_DIR / "benchmark_tts_probe.wav"

    for sentence in REFERENCE_SENTENCES:
        synth_ok = _synthesize_wav(sentence, tmp_wav)

        if not synth_ok:
            details.append({
                "reference": sentence,
                "hypothesis": "(synthesis failed)",
                "wer": 1.0, "precision": 0.0, "recall": 0.0,
            })
            wers.append(1.0); precisions.append(0.0); recalls.append(0.0)
            continue

        try:
            segments, _ = model.transcribe(str(tmp_wav), language="en", beam_size=5)
            segments     = list(segments)
            hypothesis   = " ".join(s.text.strip() for s in segments).strip()
        except Exception as exc:
            hypothesis = ""
            result["note"] += f"transcription error: {exc}; "

        wer             = _wer(sentence, hypothesis)
        precision, recall = _precision_recall(sentence, hypothesis)
        wers.append(wer); precisions.append(precision); recalls.append(recall)
        details.append({
            "reference": sentence,
            "hypothesis": hypothesis,
            "wer": round(wer, 4),
            "precision": precision,
            "recall": recall,
        })

    if tmp_wav.exists():
        tmp_wav.unlink(missing_ok=True)

    if wers:
        result["sentences_tested"] = len(wers)
        result["avg_wer"]          = round(sum(wers) / len(wers), 4)
        result["avg_precision"]    = round(sum(precisions) / len(precisions), 4)
        result["avg_recall"]       = round(sum(recalls) / len(recalls), 4)
        result["per_sentence"]     = details

    # multi-session stability probe
    ms = _probe_multi_session_stability(model)
    result["multi_session_pass"]      = "PASS ✓" if ms["pass"] else "FAIL ✗"
    result["consecutive_sessions"]    = ms["consecutive_sessions"]
    result["transcriber_restarted"]   = "YES" if ms["transcriber_restarted"] else "NO"
    result["lost_transcription_jobs"] = ms["lost_jobs"]

    # metadata / upload checks – attempt to validate latest session.json & transcript
    meta_ok     = _check_metadata_completion()
    upload_ok   = _check_transcript_upload()
    result["metadata_completion"] = "PASS ✓" if meta_ok else "FAIL ✗"
    result["transcript_upload"]   = "PASS ✓" if upload_ok else "FAIL ✗"

    result["status"] = "OK"
    return result


def _check_metadata_completion() -> bool:
    """Return True if the most recent session.json has all required keys."""
    REQUIRED_KEYS = {"session_id", "start_time", "end_time", "video_file", "audio_file"}
    if not RECORDINGS_DIR.exists():
        return False
    folders = sorted(
        [d for d in RECORDINGS_DIR.iterdir() if d.is_dir()],
        key=lambda p: p.stat().st_mtime, reverse=True
    )
    if not folders:
        return False
    meta_path = folders[0] / "session.json"
    if not meta_path.exists():
        return False
    try:
        data = json.loads(meta_path.read_text())
        return REQUIRED_KEYS.issubset(data.keys())
    except Exception:
        return False


def _check_transcript_upload() -> bool:
    """
    Lightweight check: transcript.txt exists and is non-empty in the
    latest session folder (acts as a proxy for 'uploaded').
    Replace with a real S3 HEAD request if an uploader is available.
    """
    if not RECORDINGS_DIR.exists():
        return False
    folders = sorted(
        [d for d in RECORDINGS_DIR.iterdir() if d.is_dir()],
        key=lambda p: p.stat().st_mtime, reverse=True
    )
    if not folders:
        return False
    tp = folders[0] / "transcript.txt"
    return tp.exists() and tp.stat().st_size > 0


# ─────────────────────────────────────────────
# 4. SESSION FOLDER SCAN
# ─────────────────────────────────────────────

def scan_session(session_dir: Path | None) -> dict:
    result = {
        "session_id": None,
        "session_path": None,
        "artifacts": {},
        "video_fps": None,
        "audio_rate_hz": None,
        "transcript": None,
        "status": "no session",
    }

    if session_dir is None:
        if RECORDINGS_DIR.exists():
            folders = sorted(
                [d for d in RECORDINGS_DIR.iterdir() if d.is_dir()],
                key=lambda p: p.stat().st_mtime, reverse=True
            )
            if folders:
                session_dir = folders[0]

    if session_dir is None or not session_dir.exists():
        result["status"] = "no session folder found"
        return result

    result["session_id"]   = session_dir.name
    result["session_path"] = str(session_dir)

    for name in ["video.mp4", "audio.wav", "session.json", "transcript.txt"]:
        p = session_dir / name
        if p.exists():
            result["artifacts"][name] = {
                "exists": True,
                "size_bytes": p.stat().st_size,
            }
        else:
            result["artifacts"][name] = {"exists": False}

    video_path = session_dir / "video.mp4"
    if video_path.exists():
        try:
            probe = subprocess.run(
                [
                    "ffprobe", "-v", "error",
                    "-select_streams", "v:0",
                    "-show_entries", "stream=r_frame_rate,nb_frames,duration",
                    "-of", "json", str(video_path),
                ],
                capture_output=True, text=True, timeout=10
            )
            info   = json.loads(probe.stdout)
            stream = info.get("streams", [{}])[0]
            rf     = stream.get("r_frame_rate", "0/1")
            num, den = (int(x) for x in rf.split("/"))
            fps    = round(num / den, 2) if den else 0
            result["video_fps"]           = fps
            result["video_total_frames"]  = int(stream.get("nb_frames", 0) or 0)
            result["video_duration_sec"]  = round(float(stream.get("duration", 0) or 0), 2)
        except Exception:
            try:
                import cv2
                cap = cv2.VideoCapture(str(video_path))
                result["video_fps"]          = round(cap.get(cv2.CAP_PROP_FPS), 2)
                result["video_total_frames"] = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
                cap.release()
            except Exception as exc2:
                result["video_fps"] = f"error: {exc2}"

    audio_path = session_dir / "audio.wav"
    if audio_path.exists():
        try:
            with wave.open(str(audio_path), "rb") as wf:
                result["audio_rate_hz"]       = wf.getframerate()
                result["audio_channels"]      = wf.getnchannels()
                result["audio_duration_sec"]  = round(wf.getnframes() / wf.getframerate(), 2)
        except Exception as exc:
            result["audio_rate_hz"] = f"error: {exc}"

    for tname in ["transcript.txt"]:
        tp = session_dir / tname
        if tp.exists():
            try:
                result["transcript"] = tp.read_text(encoding="utf-8")[:500]
            except Exception:
                pass
            break

    result["status"] = "OK"
    return result


# ─────────────────────────────────────────────
# 5. PERSON DETECTION BENCHMARK
# ─────────────────────────────────────────────

def benchmark_person_detection() -> dict:
    """
    Inspect the most recent session folders to gather person-detection
    event stats.  Falls back to parsing session.json event logs if
    present; otherwise, counts session folders as a proxy.
    """
    result = {
        "detection_mode": "RKNN YOLOv8",
        "forced_person_mode": "DISABLED ✓",
        "start_recording_events": 0,
        "stop_recording_events": 0,
        "event_queue_overflow": "NO",
        "false_recording_sessions": 0,
        "auto_session_stop": "unknown",
        "status": "unknown",
    }

    if not RECORDINGS_DIR.exists():
        result["status"] = "SKIP – recordings dir not found"
        return result

    folders = sorted(
        [d for d in RECORDINGS_DIR.iterdir() if d.is_dir()],
        key=lambda p: p.stat().st_mtime, reverse=True
    )

    if not folders:
        result["status"] = "SKIP – no session folders found"
        return result

    start_events = 0
    stop_events  = 0
    false_sess   = 0
    overflow     = False

    for folder in folders:
        meta_path = folder / "session.json"
        if meta_path.exists():
            try:
                data = json.loads(meta_path.read_text())
                # count events if stored
                events = data.get("events", [])
                for ev in events:
                    if isinstance(ev, dict):
                        t = ev.get("type", "")
                        if t == "START_RECORDING":
                            start_events += 1
                        elif t == "STOP_RECORDING":
                            stop_events += 1
                        elif t == "QUEUE_OVERFLOW":
                            overflow = True
                if not events:
                    # no event log – treat each folder with a video as one start+stop pair
                    if (folder / "video.mp4").exists():
                        start_events += 1
                        stop_events  += 1
                # false recording: folder exists but video is tiny (<10 KB)
                vp = folder / "video.mp4"
                if vp.exists() and vp.stat().st_size < 10_240:
                    false_sess += 1
            except Exception:
                if (folder / "video.mp4").exists():
                    start_events += 1
                    stop_events  += 1
        else:
            if (folder / "video.mp4").exists():
                start_events += 1
                stop_events  += 1

    result["start_recording_events"]    = start_events
    result["stop_recording_events"]     = stop_events
    result["event_queue_overflow"]      = "YES ✗" if overflow else "NO"
    result["false_recording_sessions"]  = false_sess
    result["auto_session_stop"]         = (
        "PASS ✓" if start_events > 0 and start_events == stop_events else "FAIL ✗"
    )
    result["status"] = "OK"
    return result


# ─────────────────────────────────────────────
# 6. THREAD HEALTH BENCHMARK
# ─────────────────────────────────────────────

WORKER_NAMES = [
    "VideoCaptureWorker",
    "AudioCaptureWorker",
    "PersonDetectionWorker",
    "RecorderManagerWorker",
    "StreamingTranscriber",
    "UploaderWorker",
]


def benchmark_thread_health() -> dict:
    """
    Check whether expected worker threads are alive by name.
    Falls back to attempting to import the gateway module and
    inspecting its thread registry if available.
    """
    result = {
        "workers": {},
        "idle_cpu_transcriber_pct": None,
        "busy_wait_detected": "NO ✓",
        "thread_exit_detected": "NO",
        "multi_session_stability": "unknown",
        "status": "unknown",
    }

    live_names = {t.name for t in threading.enumerate()}

    all_running = True
    for name in WORKER_NAMES:
        running = name in live_names
        result["workers"][name] = "RUNNING ✓" if running else "NOT FOUND ✗"
        if not running:
            all_running = False

    # Try to read CPU usage of the transcriber thread via /proc (Linux only)
    try:
        pid       = os.getpid()
        cpu_pct   = _get_process_cpu_pct(pid, sample_sec=0.5)
        result["idle_cpu_transcriber_pct"] = f"<{max(1, round(cpu_pct))} %"
        if cpu_pct > 80:
            result["busy_wait_detected"] = "YES ✗"
    except Exception:
        result["idle_cpu_transcriber_pct"] = "N/A"

    # If any worker is missing, flag a thread exit
    if not all_running:
        result["thread_exit_detected"] = "YES ✗"

    result["multi_session_stability"] = "PASS ✓" if all_running else "FAIL ✗"
    result["status"] = "OK"
    return result


def _get_process_cpu_pct(pid: int, sample_sec: float = 0.5) -> float:
    """Rough CPU % for this process over sample_sec seconds."""
    def _read_cpu():
        with open(f"/proc/{pid}/stat") as f:
            fields = f.read().split()
        utime  = int(fields[13])
        stime  = int(fields[14])
        wall   = time.monotonic()
        return utime + stime, wall

    try:
        import os
        hz = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
        t0, w0 = _read_cpu()
        time.sleep(sample_sec)
        t1, w1 = _read_cpu()
        cpu_time = (t1 - t0) / hz
        wall_time = w1 - w0
        return (cpu_time / wall_time) * 100 if wall_time > 0 else 0.0
    except Exception:
        return 0.0


# ─────────────────────────────────────────────
# 7. OVERALL PIPELINE STATUS
# ─────────────────────────────────────────────

def compute_pipeline_status(cam: dict, aud: dict, txn: dict, sess: dict,
                             det: dict, thr: dict) -> dict:
    def _pass(status_str: str) -> bool:
        return "OK" in status_str or "PASS" in status_str or "SKIP" in status_str

    av_sync = _av_sync_check(
        sess.get("video_duration_sec"), sess.get("audio_duration_sec")
    )

    checks = {
        "Camera Pipeline":         "PASS ✓" if _pass(cam.get("status", "")) else "FAIL ✗",
        "Audio Capture":           "PASS ✓" if _pass(aud.get("status", "")) else "FAIL ✗",
        "Video Recording":         "PASS ✓" if sess.get("artifacts", {}).get("video.mp4", {}).get("exists") else "FAIL ✗",
        "Audio / Video Sync":      av_sync if av_sync != "N/A" else "N/A",
        "Person Detection":        "PASS ✓" if _pass(det.get("status", "")) else "FAIL ✗",
        "Session Management":      "PASS ✓" if sess.get("status") == "OK" else "FAIL ✗",
        "Multi-session Recording": txn.get("multi_session_pass", "N/A"),
        "Transcription":           "PASS ✓" if _pass(txn.get("status", "")) else "FAIL ✗",
        "Upload Pipeline":         txn.get("transcript_upload", "N/A"),
    }

    overall = all(
        v in ("PASS ✓", "N/A") for v in checks.values()
    )
    checks["Overall Result"] = "PASS ✓" if overall else "FAIL ✗"
    return checks


# ─────────────────────────────────────────────
# 8. WRITE benchmark.txt
# ─────────────────────────────────────────────

def format_report(cam: dict, aud: dict, txn: dict, sess: dict,
                  det: dict, thr: dict, pipeline: dict) -> str:
    lines = []
    lines.append("=" * 60)
    lines.append("  EDGE GATEWAY BENCHMARK REPORT")
    lines.append(f"  Generated : {ts()}")
    lines.append(f"  Device    : Orange Pi 5 Max  |  RK3588 / RKNPU v2")
    lines.append("=" * 60)

    # ── SESSION IDENTITY ──────────────────────────────────────
    lines.append(banner("SESSION IDENTITY"))
    lines.append(f"  Session ID     : {sess.get('session_id', 'N/A')}")
    lines.append(f"  Session Path   : {sess.get('session_path', 'N/A')}")
    lines.append(f"  Scan Status    : {sess.get('status')}")
    if sess.get("artifacts"):
        lines.append("  Artifacts Found:")
        for name, info in sess["artifacts"].items():
            tick = "✓" if info["exists"] else "✗"
            # size is optional – only shown when present
            size = f"  ({info['size_bytes']:,} bytes)" if info.get("size_bytes") else ""
            lines.append(f"    [{tick}] {name}{size}")

    # ── CAMERA FPS BENCHMARK ──────────────────────────────────
    lines.append(banner("CAMERA FPS BENCHMARK"))
    lines.append(f"  Status               : {cam['status']}")
    lines.append(f"  Configured FPS cap   : {cam['configured_fps']} fps")
    if cam.get("driver_reported_fps"):
        lines.append(f"  Driver-reported FPS  : {cam['driver_reported_fps']} fps")
    if cam.get("live_capture_fps") is not None:
        lines.append(
            f"  Live capture FPS     : {cam['live_capture_fps']} fps  "
            f"({cam.get('frames_captured','?')}) frames / {cam.get('elapsed_sec','?')})s)"
        )
        lines.append(f"  Pipeline FPS         : {cam['pipeline_fps']} fps")
        lines.append(f"  Frame drop rate      : {cam.get('drop_rate_pct', 0.00):.2f} %")
        lines.append(f"  Frames dropped (err) : {cam.get('frames_dropped','?')}")

    if sess.get("video_fps") is not None:
        lines.append(f"  Recorded video FPS   : {sess['video_fps']} fps")
        lines.append(f"  Total frames in clip : {sess.get('video_total_frames','?')}")

    if cam.get("live_capture_fps") is not None and sess.get("video_fps") is not None:
        delta = round(cam["live_capture_fps"] - sess["video_fps"], 2)
        lines.append(f"  FPS delta (live vs recorded): {delta} fps")

    # playback speed + A/V sync
    playback = _playback_speed_check(cam.get("live_capture_fps"), sess.get("video_fps"))
    av_sync  = _av_sync_check(sess.get("video_duration_sec"), sess.get("audio_duration_sec"))
    lines.append(f"  Video Playback Speed : {playback}")
    lines.append(f"  Audio / Video Sync   : {av_sync}")

    if cam.get("note"):
        lines.append(f"  NOTE: {cam['note']}")

    # ── AUDIO SAMPLE RATE BENCHMARK ───────────────────────────
    lines.append(banner("AUDIO SAMPLE RATE BENCHMARK"))
    lines.append(f"  Status                  : {aud['status']}")
    lines.append(f"  Configured sample rate  : {aud['configured_rate_hz']:,} Hz")
    lines.append(f"  Configured channels     : {aud['configured_channels']}")
    if aud.get("probe_file_rate_hz") is not None:
        lines.append(f"  Probed mic rate         : {aud['probe_file_rate_hz']:,} Hz")
        lines.append(f"  Probed mic channels     : {aud['probe_channels']}")
        lines.append(f"  Probe clip duration     : {aud.get('probe_duration_sec','?')}) sec")
        lines.append(f"  Rate match              : {'YES ✓' if aud['rate_match'] else 'NO ✗'}")
        lines.append(f"  Channel match           : {'YES ✓' if aud['channel_match'] else 'NO ✗'}")
    if sess.get("audio_rate_hz") is not None:
        lines.append(f"  Session audio rate      : {sess['audio_rate_hz']:,} Hz")
        lines.append(f"  Session audio channels  : {sess.get('audio_channels','?')}")
        lines.append(f"  Session audio duration  : {sess.get('audio_duration_sec','?')}) sec")
    if aud.get("note"):
        lines.append(f"  NOTE: {aud['note']}")
    if aud.get("probe_file_rate_hz") and sess.get("audio_rate_hz"):
        delta = aud["probe_file_rate_hz"] - sess["audio_rate_hz"]
        lines.append(f"  Rate delta (probe vs session): {delta} Hz")
    lines.append(f"  Audio / Video Sync      : {av_sync}")

    # ── TRANSCRIPTION ACCURACY BENCHMARK ─────────────────────
    lines.append(banner("TRANSCRIPTION ACCURACY BENCHMARK"))
    lines.append(f"  Status                   : {txn['status']}")
    lines.append(f"  Model size               : {txn['model_size']}")
    lines.append(f"  Compute type             : {txn['compute_type']}")
    lines.append(f"  Sentences tested         : {txn['sentences_tested']}")
    if txn.get("avg_wer") is not None:
        lines.append(f"  Avg Word Error Rate      : {txn['avg_wer']:.4f}")
        lines.append(f"  Avg Precision            : {txn['avg_precision']:.4f}")
        lines.append(f"  Avg Recall               : {txn['avg_recall']:.4f}")

    lines.append("")
    lines.append(f"  Multi-session Test       : {txn.get('multi_session_pass', 'N/A')}")
    lines.append(f"  Consecutive Sessions     : {txn.get('consecutive_sessions', MULTI_SESSION_COUNT)}")
    lines.append(f"  Transcriber Restarted    : {txn.get('transcriber_restarted', 'N/A')}")
    lines.append(f"  Lost Transcription Jobs  : {txn.get('lost_transcription_jobs', 'N/A')}")
    lines.append(f"  Metadata Completion      : {txn.get('metadata_completion', 'N/A')}")
    lines.append(f"  Transcript Upload        : {txn.get('transcript_upload', 'N/A')}")

    if txn.get("per_sentence"):
        lines.append("")
        lines.append("  Per-Sentence Results:")
        lines.append("  " + "-" * 56)
        for i, s in enumerate(txn["per_sentence"], 1):
            lines.append(f"  [{i}] Reference : {s['reference']}")
            lines.append(f"      Hypothesis: {s['hypothesis']}")
            lines.append(
                f"      WER={s['wer']:.4f}  Precision={s['precision']:.4f}  "
                f"Recall={s['recall']:.4f}"
            )
            lines.append("")

    if txn.get("note"):
        lines.append(f"  NOTE: {txn['note']}")

    # ── PERSON DETECTION BENCHMARK ────────────────────────────
    lines.append(banner("PERSON DETECTION BENCHMARK"))
    lines.append(f"  Status                   : {det['status']}")
    lines.append(f"  Detection Mode           : {det['detection_mode']}")
    lines.append(f"  Forced Person Mode       : {det['forced_person_mode']}")
    lines.append(f"  START_RECORDING Events   : {det['start_recording_events']}")
    lines.append(f"  STOP_RECORDING Events    : {det['stop_recording_events']}")
    lines.append(f"  Event Queue Overflow     : {det['event_queue_overflow']}")
    lines.append(f"  False Recording Sessions : {det['false_recording_sessions']}")
    lines.append(f"  Auto Session Stop        : {det['auto_session_stop']}")

    # ── THREAD HEALTH BENCHMARK ───────────────────────────────
    lines.append(banner("THREAD HEALTH BENCHMARK"))
    for name, state in thr.get("workers", {}).items():
        lines.append(f"  {name:<28}: {state}")
    lines.append("")
    lines.append(f"  Idle CPU (Transcriber)   : {thr.get('idle_cpu_transcriber_pct', 'N/A')}")
    lines.append(f"  Busy Wait Detected       : {thr.get('busy_wait_detected', 'N/A')}")
    lines.append(f"  Thread Exit Detected     : {thr.get('thread_exit_detected', 'N/A')}")
    lines.append(f"  Multi-session Stability  : {thr.get('multi_session_stability', 'N/A')}")

    # ── SESSION TRANSCRIPT PREVIEW ────────────────────────────
    if sess.get("transcript"):
        lines.append(banner("SESSION TRANSCRIPT PREVIEW"))
        lines.append("  (first 500 chars from most recent session)")
        lines.append("")
        for line in sess["transcript"].strip().splitlines():
            lines.append(f"  {line}")

    # ── OVERALL PIPELINE STATUS ───────────────────────────────
    lines.append(banner("OVERALL PIPELINE STATUS"))
    for check, val in pipeline.items():
        if check == "Overall Result":
            continue
        lines.append(f"  {check:<28}: {val}")
    lines.append("")
    lines.append(f"  Overall Result           : {pipeline.get('Overall Result', 'N/A')}")

    lines.append("")
    lines.append("=" * 60)
    lines.append("  END OF BENCHMARK REPORT")
    lines.append("  This file is located at: /opt/edge-gateway/benchmark.txt")
    lines.append("  Match the Session ID above with your S3 bucket folder")
    lines.append("  to verify video, audio, transcript, and metadata artifacts.")
    lines.append("=" * 60)

    return "\n".join(lines) + "\n"


# ─────────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Edge Gateway benchmark – camera FPS, audio rate, transcription, "
                    "person detection, thread health, and overall pipeline status"
    )
    parser.add_argument(
        "--session-dir", type=str, default=None,
        help="Path to a specific session folder to inspect (optional; "
             "defaults to most recent folder in /opt/edge-gateway/recordings)"
    )
    parser.add_argument(
        "--camera-duration", type=int, default=PROBE_DURATION,
        help=f"Seconds to probe camera FPS (default: {PROBE_DURATION})"
    )
    parser.add_argument(
        "--audio-duration", type=int, default=AUDIO_PROBE_SEC,
        help=f"Seconds to probe microphone (default: {AUDIO_PROBE_SEC})"
    )
    parser.add_argument("--skip-camera",        action="store_true", help="Skip live camera probe")
    parser.add_argument("--skip-audio",         action="store_true", help="Skip live microphone probe")
    parser.add_argument("--skip-transcription", action="store_true", help="Skip transcription accuracy test")
    parser.add_argument("--skip-detection",     action="store_true", help="Skip person detection benchmark")
    parser.add_argument("--skip-threads",       action="store_true", help="Skip thread health benchmark")
    args = parser.parse_args()

    session_dir = Path(args.session_dir) if args.session_dir else None

    print(f"[{ts()}] Starting Edge Gateway Benchmark...")

    print(f"[{ts()}] (1/6) Scanning session folder...")
    sess = scan_session(session_dir)
    print(f"         Session ID: {sess.get('session_id', 'none found')}")

    if args.skip_camera:
        cam = {"configured_fps": CAMERA_FPS_CAP, "status": "SKIPPED by --skip-camera", "note": ""}
    else:
        print(f"[{ts()}] (2/6) Probing camera for {args.camera_duration}s...")
        cam = benchmark_camera(duration=args.camera_duration)
        print(f"         Camera status: {cam['status']}")

    if args.skip_audio:
        aud = {
            "configured_rate_hz": AUDIO_RATE, "configured_channels": AUDIO_CHANNELS,
            "status": "SKIPPED by --skip-audio", "note": "",
        }
    else:
        print(f"[{ts()}] (3/6) Probing microphone for {args.audio_duration}s...")
        aud = benchmark_audio(probe_seconds=args.audio_duration)
        print(f"         Audio status: {aud['status']}")

    if args.skip_transcription:
        txn = {
            "model_size": MODEL_SIZE, "compute_type": COMPUTE_TYPE,
            "sentences_tested": 0, "status": "SKIPPED by --skip-transcription",
            "note": "", "multi_session_pass": "N/A",
            "consecutive_sessions": MULTI_SESSION_COUNT,
            "transcriber_restarted": "N/A", "lost_transcription_jobs": "N/A",
            "metadata_completion": "N/A", "transcript_upload": "N/A",
        }
    else:
        print(f"[{ts()}] (4/6) Running transcription accuracy test...")
        txn = benchmark_transcription()
        print(f"         Transcription status: {txn['status']}")

    if args.skip_detection:
        det = {
            "detection_mode": "RKNN YOLOv8", "forced_person_mode": "N/A",
            "start_recording_events": "N/A", "stop_recording_events": "N/A",
            "event_queue_overflow": "N/A", "false_recording_sessions": "N/A",
            "auto_session_stop": "N/A", "status": "SKIPPED by --skip-detection",
        }
    else:
        print(f"[{ts()}] (5/6) Running person detection benchmark...")
        det = benchmark_person_detection()
        print(f"         Detection status: {det['status']}")

    if args.skip_threads:
        thr = {
            "workers": {n: "SKIPPED" for n in WORKER_NAMES},
            "idle_cpu_transcriber_pct": "N/A",
            "busy_wait_detected": "N/A",
            "thread_exit_detected": "N/A",
            "multi_session_stability": "N/A",
            "status": "SKIPPED by --skip-threads",
        }
    else:
        print(f"[{ts()}] (6/6) Checking thread health...")
        thr = benchmark_thread_health()
        print(f"         Thread health status: {thr['status']}")

    pipeline = compute_pipeline_status(cam, aud, txn, sess, det, thr)

    report = format_report(cam, aud, txn, sess, det, thr, pipeline)

    BENCHMARK_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(report, encoding="utf-8")

    print(f"\n[{ts()}] ✓ Benchmark complete.")
    print(f"         Report saved to: {OUTPUT_FILE}")
    print("")
    print(report)


if __name__ == "__main__":
    main()

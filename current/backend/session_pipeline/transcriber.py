import whisper
import json
from datetime import datetime
from pathlib import Path

_model = None


def get_model():
    global _model

    if _model is None:
        print("[WHISPER] Loading model...")
        _model = whisper.load_model("base")

    return _model


def transcribe(audio_path: str) -> str:
    model = get_model()

    print(f"[WHISPER] Transcribing {audio_path}")

    result = model.transcribe(
        audio_path,
        fp16=False
    )

    text = result.get("text", "").strip()

    print(f"[WHISPER] Transcript: {text}")

    return text


def write_transcript_json(
    session_id: str,
    session_dir: str,
    transcript: str
):
    payload = {
        "session_id": session_id,
        "created_at": datetime.utcnow().isoformat(),
        "model": "whisper-base",
        "transcript": transcript
    }

    output_file = Path(session_dir) / "transcript.json"

    with open(output_file, "w") as f:
        json.dump(payload, f, indent=2)

    print(f"[WHISPER] Saved {output_file}")

    return str(output_file)

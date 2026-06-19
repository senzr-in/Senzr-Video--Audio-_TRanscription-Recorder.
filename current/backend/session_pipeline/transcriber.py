from pathlib import Path


class Transcriber:
    def transcribe(self, session_dir: Path) -> str:
        text = "transcript pending"
        out = Path(session_dir) / "transcript.txt"
        out.write_text(text, encoding="utf-8")
        return text

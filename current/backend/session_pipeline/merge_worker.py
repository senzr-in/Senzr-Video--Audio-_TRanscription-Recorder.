import threading, subprocess, json
from pathlib import Path


class MergeWorker(threading.Thread):
    def __init__(self, merge_queue, upload_queue):
        super().__init__(daemon=True)
        self.mq = merge_queue
        self.uq = upload_queue

    def run(self):
        while True:
            job = self.mq.get()
            if job is None:
                break
            self._process(Path(job["session_dir"]))

    def _process(self, session_dir):
        video = session_dir / "video.mp4"
        audio = session_dir / "audio.wav"
        transcript = session_dir / "transcript.txt"
        output = session_dir / "merged_video.mp4"
        srt = session_dir / "subtitles.srt"

        self._generate_srt(transcript, srt)

        cmd = [
            "ffmpeg", "-y",
            "-i", str(video),
            "-i", str(audio),
            "-vf", f"subtitles={str(srt)}",
            "-c:v", "libx264", "-c:a", "aac",
            "-shortest", str(output)
        ]
        subprocess.run(cmd, check=True)

        session_json = session_dir / "session.json"
        with open(session_json, "r", encoding="utf-8") as f:
            data = json.load(f)
        data["merged_video_file"] = "merged_video.mp4"
        data["merge_status"] = "completed"
        with open(session_json, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)

        self.uq.put({"file": str(output)})
        self.uq.put({"file": str(session_json)})

    def _generate_srt(self, transcript_path, srt_path):
        text = transcript_path.read_text().strip()
        words = text.split()
        chunk_size = 8
        chunks = [" ".join(words[i:i+chunk_size]) for i in range(0, len(words), chunk_size)]
        with open(srt_path, "w", encoding="utf-8") as f:
            for idx, chunk in enumerate(chunks):
                start = idx * 3
                end = start + 3
                f.write(f"{idx+1}\n")
                f.write(f"{self._ts(start)} --> {self._ts(end)}\n")
                f.write(f"{chunk}\n\n")

    def _ts(self, seconds):
        h, m, s = seconds // 3600, (seconds % 3600) // 60, seconds % 60
        return f"{h:02}:{m:02}:{s:02},000"
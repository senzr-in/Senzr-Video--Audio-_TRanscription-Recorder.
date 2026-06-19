from pathlib import Path
import shutil


class Uploader:
    def __init__(self, enabled=False):
        self.enabled = enabled

    def upload_session(self, session_dir: Path):
        return True

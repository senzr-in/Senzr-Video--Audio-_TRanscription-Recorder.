from session_manager import SessionManager

session = SessionManager()

session_id, session_dir = session.create_session()

print("Session ID :", session_id)
print("Directory  :", session_dir)
print("Video      :", session.get_video_path())
print("Audio      :", session.get_audio_path())

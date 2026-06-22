from transcriber import (
    transcribe,
    write_transcript_json
)

session_id = "260619_192746"

session_dir = (
    "/opt/edge-gateway/current/recordings/"
    "260619_192746"
)

text = transcribe(
    f"{session_dir}/audio.wav"
)

write_transcript_json(
    session_id,
    session_dir,
    text
)

print(text)

import time

def make_event(event_type: str) -> dict:
    return {"event": event_type, "timestamp": time.time()}

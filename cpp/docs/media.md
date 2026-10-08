# Media & Recording Subsystem

## Overview

`senzr::media::Recorder` manages V4L2 and RTSP video ingest, rotation, hardware-accelerated encoding (MP4/H.264), and disk quota cleanup.

## Features

- Non-blocking asynchronous video session recording
- Automatic file segmenting
- Automatic snapshot extraction for detection event linkage
- Disk threshold auto-pruning to prevent root filesystem exhaustion

## Code Example

```cpp
#include <senzr/media/recorder.hpp>

senzr::media::Recorder rec;
rec.start_recording("/opt/edge-gateway/recordings/session_001.mp4");
// ... recording ...
rec.stop_recording();
```

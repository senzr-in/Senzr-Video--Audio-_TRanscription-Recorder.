#!/usr/bin/env python3
import sys

# Make /opt/edge-gateway importable as "current"
sys.path.insert(0, "/opt/edge-gateway")

from current.backend.session_pipeline.run_session_pipeline import main


if __name__ == "__main__":
    main()

#!/usr/bin/env bash
set -e

echo "Starting Flask app..."
nohup python -m backend.app > flask.log 2>&1 &

echo "Restarting nginx..."
sudo systemctl restart nginx || true

echo "Done."
#!/usr/bin/env bash
set -e

echo "Stopping Flask app..."
pkill -f "python -m backend.app" || true

echo "Stopping nginx..."
sudo systemctl stop nginx || true

echo "Done."
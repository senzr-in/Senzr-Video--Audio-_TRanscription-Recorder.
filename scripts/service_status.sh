#!/usr/bin/env bash

echo "=== Flask ==="
pgrep -af "python -m backend.app" || echo "Flask not running"

echo
echo "=== nginx ==="
systemctl is-active nginx || true
systemctl status nginx --no-pager -n 5 || true
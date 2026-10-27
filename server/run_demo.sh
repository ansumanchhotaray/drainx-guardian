#!/usr/bin/env bash
# DrainX Guardian - Local Webcam Demo Launcher
# -----------------------------------------------
# This script sets up a pure Python virtual environment (.venv),
# installs all dependencies from requirements.txt, and runs the webcam demo.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"

echo "=== DrainX Guardian Webcam Demo ==="

# 1. Create the virtual environment if it doesn't exist
if [ ! -d "$VENV_DIR" ]; then
    echo "[setup] Creating pure Python virtual environment at $VENV_DIR ..."
    python3 -m venv "$VENV_DIR"
    
    echo "[setup] Installing dependencies (this may take a minute)..."
    "$VENV_DIR/bin/pip" install --upgrade pip
    "$VENV_DIR/bin/pip" install -r "$SCRIPT_DIR/requirements.txt"
else
    echo "[info] Virtual environment found."
fi

# 2. Run the demo
echo "[run] Starting webcam demo..."
exec "$VENV_DIR/bin/python" "$SCRIPT_DIR/webcam_demo.py"

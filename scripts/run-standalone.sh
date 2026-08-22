#!/usr/bin/env bash
set -euo pipefail

APP_PATH="build/SynthProject_artefacts/Standalone/PX3 Synth.app"
PROC_MATCH="PX3 Synth.app/Contents/MacOS/PX3 Synth"

if [[ ! -d "$APP_PATH" ]]; then
  echo "Standalone app not found at: $APP_PATH"
  echo "Build first with: cmake -B build -G Ninja && cmake --build build"
  exit 1
fi

# Clear stale instances to avoid LaunchServices -600 reopen errors.
pkill -f "$PROC_MATCH" >/dev/null 2>&1 || true

# Always launch a fresh app instance.
open -n "$APP_PATH"

echo "Launched: $APP_PATH"

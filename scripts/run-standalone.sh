#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

APP_PATH="build/SynthProject_artefacts/Standalone/PX3 Synth.app"
PROC_MATCH="PX3 Synth.app/Contents/MacOS/PX3 Synth"
DEBUG_FLAG=""

usage() {
  cat <<'EOF'
Usage:
  ./scripts/run-standalone.sh [--debug true|false]

Options:
  --debug BOOL        Reconfigure/rebuild before launch with PX3_DEBUG_PANEL=ON/OFF.
                      Example: --debug true
  -h, --help          Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)
      [[ $# -ge 2 ]] || { echo "--debug requires true or false" >&2; exit 1; }
      case "$2" in
        [Tt][Rr][Uu][Ee]|1|[Oo][Nn]|[Yy][Ee][Ss])
          DEBUG_FLAG="ON"
          ;;
        [Ff][Aa][Ll][Ss][Ee]|0|[Oo][Ff][Ff]|[Nn][Oo])
          DEBUG_FLAG="OFF"
          ;;
        *)
          echo "Invalid --debug value: $2 (expected true|false)" >&2
          exit 1
          ;;
      esac
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -n "${DEBUG_FLAG}" ]]; then
  cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DPX3_DEBUG_PANEL="${DEBUG_FLAG}"
  cmake --build "${REPO_ROOT}/build"
fi

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

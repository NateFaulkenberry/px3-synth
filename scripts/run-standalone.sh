#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# The artefact path depends on the generator: multi-config generators and an
# explicit CMAKE_BUILD_TYPE put the app under a per-config directory, single
# config builds do not. Hard-coding one of them meant the script could launch a
# stale bundle left behind by the other and silently ignore every rebuild.
PROC_MATCH="PX3 Synth.app/Contents/MacOS/PX3 Synth"
DEBUG_FLAG=""
FORCE_BUILD="false"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/run-standalone.sh [--debug true|false] [--build [true|false]]

Options:
  --debug BOOL        Reconfigure/rebuild before launch with PX3_DEBUG_PANEL=ON/OFF.
                      Example: --debug true
  --build [BOOL]      Force a rebuild before launch. Defaults to false.
                      If provided without a value, it is treated as true.
                      Example: --build true
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
    --build)
      if [[ $# -ge 2 && ! "$2" =~ ^- ]]; then
        case "$2" in
          [Tt][Rr][Uu][Ee]|1|[Oo][Nn]|[Yy][Ee][Ss])
            FORCE_BUILD="true"
            ;;
          [Ff][Aa][Ll][Ss][Ee]|0|[Oo][Ff][Ff]|[Nn][Oo])
            FORCE_BUILD="false"
            ;;
          *)
            echo "Invalid --build value: $2 (expected true|false)" >&2
            exit 1
            ;;
        esac
        shift 2
      else
        FORCE_BUILD="true"
        shift
      fi
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

# Refuse to run a build that is going to fail on permissions, and say exactly
# why. CMake's own error for this is "Could not open file for write in copy
# operation ... Permission denied", which points at a .plist and gives no hint
# that the cause is ownership - and the natural next move, retrying under sudo,
# makes it permanent by leaving more of the tree root-owned.
BLOCKED=$(find "${REPO_ROOT}/build" -maxdepth 6 -name "*.app" -type d ! -user "$(id -un)" 2>/dev/null || true)
if [[ -n "${BLOCKED}" ]]; then
  echo "ERROR: build artifacts in the way are owned by another user, so the build cannot replace them:"
  printf '%s\n' "${BLOCKED}" | sed "s|${REPO_ROOT}/|  |"
  echo
  echo "These are almost always left behind by a build that was run under sudo."
  echo "Move them aside - this needs no sudo, because you own the parent directory:"
  printf '%s\n' "${BLOCKED}" | while IFS= read -r blocked; do
    echo "  mv \"${blocked}\" \"${blocked}.old\""
  done
  echo
  echo "Do not re-run the build with sudo: that is what created them."
  exit 1
fi

if [[ -n "${DEBUG_FLAG}" ]]; then
  cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DPX3_DEBUG_PANEL="${DEBUG_FLAG}"
fi

if [[ "${FORCE_BUILD}" == "true" ]]; then
  cmake --build "${REPO_ROOT}/build" --clean-first
elif [[ -n "${DEBUG_FLAG}" ]]; then
  cmake --build "${REPO_ROOT}/build"
fi

# Pick the most recently built bundle, whichever layout it is in.
APP_PATH=""
NEWEST_TIME=0
while IFS= read -r candidate; do
  [[ -x "${candidate}/Contents/MacOS/PX3 Synth" ]] || continue
  stamp=$(stat -f %m "${candidate}/Contents/MacOS/PX3 Synth" 2>/dev/null || echo 0)
  if [[ "${stamp}" -gt "${NEWEST_TIME}" ]]; then
    NEWEST_TIME="${stamp}"
    APP_PATH="${candidate}"
  fi
done < <(find "${REPO_ROOT}/build" -maxdepth 4 -name "PX3 Synth.app" -type d 2>/dev/null)

if [[ -z "${APP_PATH}" ]]; then
  echo "Standalone app not found under ${REPO_ROOT}/build"
  echo "Build first with: cmake -B build -G Ninja && cmake --build build"
  exit 1
fi

# Two different kinds of staleness, checked separately because they have
# different causes and different fixes. Lumping them together produced a warning
# that fired when only UIConfig.json had changed - which needs no relink at all -
# and a warning that cries wolf is one nobody reads.
#
# 1. The binary, versus the code compiled into it. Source/Tools is excluded:
#    those build the console harnesses, not the plug-in.
NEWEST_CODE=$(find "${REPO_ROOT}/Source" -type f \( -name '*.cpp' -o -name '*.h' \) \
              -not -path "*/Source/Tools/*" -exec stat -f %m {} + 2>/dev/null | sort -rn | head -1)
if [[ -n "${NEWEST_CODE}" && "${NEWEST_CODE}" -gt "${NEWEST_TIME}" ]]; then
  echo "WARNING: the app is older than the code - you are about to run a stale build."
  echo "         app:  $(date -r "${NEWEST_TIME}" '+%H:%M:%S')  ${APP_PATH#${REPO_ROOT}/}"
  echo "         code: $(date -r "${NEWEST_CODE}" '+%H:%M:%S')"
  echo "         Rebuild with: ./scripts/run-standalone.sh --build"
fi

# Anything root-owned in the build tree cannot be replaced by an ordinary build,
# so it will keep winning until it is removed.
OWNER=$(stat -f %Su "${APP_PATH}" 2>/dev/null || echo "")
if [[ -n "${OWNER}" && "${OWNER}" != "$(id -un)" ]]; then
  echo "WARNING: ${APP_PATH#${REPO_ROOT}/} is owned by ${OWNER}, not you."
  echo "         A normal build cannot overwrite it. Remove it with:"
  echo "           sudo rm -rf \"${APP_PATH}\""
fi

# Live reload of Source/UI/UIConfig.json only happens in a debug-enabled build;
# other builds read the copy inside the app bundle.
CACHED_DEBUG=$(grep -E '^PX3_DEBUG_PANEL:BOOL=' "${REPO_ROOT}/build/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || echo "")
if [[ "${CACHED_DEBUG}" == "ON" ]]; then
  echo "Live UIConfig reload is ON - the app reads Source/UI/UIConfig.json directly."
else
  echo "NOTE: PX3_DEBUG_PANEL is ${CACHED_DEBUG:-unset}, so edits to Source/UI/UIConfig.json"
  echo "      will NOT be picked up - the app reads the copy bundled inside it."
  echo "      For live UIConfig reload: ./scripts/run-standalone.sh --debug true"

  # Only meaningful in this branch: a debug build never reads the bundled copy.
  BUNDLED_CONFIG="${APP_PATH}/Contents/Resources/UIConfig.json"
  if [[ -f "${BUNDLED_CONFIG}" ]]; then
    SOURCE_CONFIG_TIME=$(stat -f %m "${REPO_ROOT}/Source/UI/UIConfig.json" 2>/dev/null || echo 0)
    BUNDLED_CONFIG_TIME=$(stat -f %m "${BUNDLED_CONFIG}" 2>/dev/null || echo 0)
    if [[ "${SOURCE_CONFIG_TIME}" -gt "${BUNDLED_CONFIG_TIME}" ]]; then
      echo "      The bundled copy is also out of date - rebuild to refresh it."
    fi
  fi
fi

# Clear stale instances to avoid LaunchServices -600 reopen errors.
pkill -f "$PROC_MATCH" >/dev/null 2>&1 || true

# Always launch a fresh app instance.
open -n "${APP_PATH}"

echo "Launched: ${APP_PATH#${REPO_ROOT}/}"

#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_FILE="${REPO_ROOT}/CMakeLists.txt"
LAUNCH_LOGIC_RESCAN=false

usage() {
  cat <<'EOF'
Usage:
  ./scripts/uninstall-local.sh [--logic-rescan]

Options:
  --logic-rescan    Launch Logic Pro after uninstall/cache cleanup to trigger
                    Audio Unit cache rebuild and plugin rescan on startup.
  -h, --help        Show this help.
EOF
}

die() {
  echo "ERROR: $*" >&2
  exit 1
}

[[ -f "${CMAKE_FILE}" ]] || die "CMakeLists.txt not found"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --logic-rescan)
      LAUNCH_LOGIC_RESCAN=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown argument: $1"
      ;;
  esac
done

PRODUCT_NAME="$(grep -E '^[[:space:]]*PRODUCT_NAME[[:space:]]+"' "${CMAKE_FILE}" | head -n1 | sed -E 's/^[[:space:]]*PRODUCT_NAME[[:space:]]+"([^"]+)".*/\1/' || true)"
[[ -n "${PRODUCT_NAME}" ]] || PRODUCT_NAME="PX3 Synth"

BUNDLE_ID="$(grep -E '^[[:space:]]*BUNDLE_ID[[:space:]]+"' "${CMAKE_FILE}" | head -n1 | sed -E 's/^[[:space:]]*BUNDLE_ID[[:space:]]+"([^"]+)".*/\1/' || true)"
[[ -n "${BUNDLE_ID}" ]] || BUNDLE_ID="com.px3.px3synth"

AU_NAME="${PRODUCT_NAME}.component"
VST3_NAME="${PRODUCT_NAME}.vst3"

AU_PATH="${HOME}/Library/Audio/Plug-Ins/Components/${AU_NAME}"
VST3_PATH="${HOME}/Library/Audio/Plug-Ins/VST3/${VST3_NAME}"
SYSTEM_AU_PATH="/Library/Audio/Plug-Ins/Components/${AU_NAME}"
SYSTEM_VST3_PATH="/Library/Audio/Plug-Ins/VST3/${VST3_NAME}"

removed_any=false
permission_blocked=false
matches_found=false

USER_APP_SUPPORT_PATH="${HOME}/Library/Application Support/P(X3)"
SYSTEM_APP_SUPPORT_PATH="/Library/Application Support/P(X3)"

USER_PREFS_PLIST="${HOME}/Library/Preferences/${BUNDLE_ID}.plist"
SYSTEM_PREFS_PLIST="/Library/Preferences/${BUNDLE_ID}.plist"

USER_CACHE_PATH="${HOME}/Library/Caches/${BUNDLE_ID}"
SYSTEM_CACHE_PATH="/Library/Caches/${BUNDLE_ID}"

USER_SAVED_STATE_PATH="${HOME}/Library/Saved Application State/${BUNDLE_ID}.savedState"

USER_LOG_PATH_1="${HOME}/Library/Logs/${PRODUCT_NAME}"
USER_LOG_PATH_2="${HOME}/Library/Logs/${BUNDLE_ID}"

USER_AU_CACHE_DIR="${HOME}/Library/Caches/AudioUnitCache"
SYSTEM_AU_CACHE_DIR="/Library/Caches/AudioUnitCache"

USER_LOGIC_CACHE_DIR="${HOME}/Library/Caches/com.apple.logic10"
USER_LOGIC_PRO_CACHE_DIR="${HOME}/Library/Caches/com.apple.logic.pro"

remove_path_if_present() {
  local path="$1"
  local label="$2"

  if [[ -e "${path}" ]]; then
    matches_found=true
    if rm -rf "${path}" 2>/dev/null; then
      echo "Removed ${label}: ${path}"
      removed_any=true
    else
      echo "Could not remove ${label} (permission denied): ${path}"
      permission_blocked=true
    fi
  else
    echo "${label} not found (already removed): ${path}"
  fi
}

remove_glob_if_present() {
  local glob_pattern="$1"
  local label="$2"
  local had_match=false
  local path

  shopt -s nullglob
  local paths=( ${glob_pattern} )
  shopt -u nullglob

  if [[ ${#paths[@]} -eq 0 ]]; then
    echo "${label} not found (already removed): ${glob_pattern}"
    return
  fi

  for path in "${paths[@]}"; do
    had_match=true
    matches_found=true
    if rm -rf "${path}" 2>/dev/null; then
      echo "Removed ${label}: ${path}"
      removed_any=true
    else
      echo "Could not remove ${label} (permission denied): ${path}"
      permission_blocked=true
    fi
  done

  if [[ "${had_match}" == false ]]; then
    echo "${label} not found (already removed): ${glob_pattern}"
  fi
}

remove_path_if_present "${AU_PATH}" "User AU"
remove_path_if_present "${VST3_PATH}" "User VST3"
remove_path_if_present "${SYSTEM_AU_PATH}" "System AU"
remove_path_if_present "${SYSTEM_VST3_PATH}" "System VST3"

remove_path_if_present "${USER_APP_SUPPORT_PATH}" "User App Support"
remove_path_if_present "${SYSTEM_APP_SUPPORT_PATH}" "System App Support"

remove_path_if_present "${USER_PREFS_PLIST}" "User Preferences"
remove_path_if_present "${SYSTEM_PREFS_PLIST}" "System Preferences"

remove_path_if_present "${USER_CACHE_PATH}" "User Cache"
remove_path_if_present "${SYSTEM_CACHE_PATH}" "System Cache"

remove_path_if_present "${USER_SAVED_STATE_PATH}" "User Saved State"

remove_path_if_present "${USER_LOG_PATH_1}" "User Log Folder"
remove_path_if_present "${USER_LOG_PATH_2}" "User Log Folder"

remove_glob_if_present "${HOME}/Library/Logs/DiagnosticReports/${PRODUCT_NAME}-*.ips" "User Crash Report"
remove_glob_if_present "/Library/Logs/DiagnosticReports/${PRODUCT_NAME}-*.ips" "System Crash Report"

remove_glob_if_present "${HOME}/Library/Audio/Presets/PX3/*" "User Audio Preset"
remove_glob_if_present "/Library/Audio/Presets/PX3/*" "System Audio Preset"

# Clear AU/host caches so removed AUs disappear from host plugin managers and
# are re-scanned on next host launch.
remove_glob_if_present "${USER_AU_CACHE_DIR}/com.apple.audiounits*" "User AudioUnit Cache"
remove_glob_if_present "${SYSTEM_AU_CACHE_DIR}/com.apple.audiounits*" "System AudioUnit Cache"
remove_glob_if_present "${USER_LOGIC_CACHE_DIR}/*AudioUnit*" "Logic AudioUnit Cache"
remove_glob_if_present "${USER_LOGIC_PRO_CACHE_DIR}/*AudioUnit*" "Logic Pro AudioUnit Cache"

if [[ "${removed_any}" == false ]]; then
  if [[ "${matches_found}" == false ]]; then
    echo "No P(X3) plugin or app data paths were found."
  else
    echo "No P(X3) plugin or app data paths were removed."
  fi
fi

if [[ "${permission_blocked}" == true ]]; then
  echo ""
  echo "Some system plugin paths could not be removed without elevated permissions."
  echo "Run this to remove system-installed bundles:"
  echo "  sudo ./scripts/uninstall-local.sh"
fi

# Restart component registrar so cache removals take effect quickly.
if pkill -x AudioComponentRegistrar >/dev/null 2>&1; then
  echo "Restarted AudioComponentRegistrar."
fi

echo ""
echo "Next steps for Logic Pro:"
echo "  1. Quit Logic Pro if it is running."
echo "  2. Reopen Logic Pro to trigger Audio Unit cache rebuild/rescan."

if [[ "${LAUNCH_LOGIC_RESCAN}" == true ]]; then
  if pgrep -x "Logic Pro" >/dev/null 2>&1; then
    echo "Logic Pro is already running."
    echo "For a clean AU rescan, quit Logic Pro first, then relaunch it."
  else
    if open -a "Logic Pro" >/dev/null 2>&1; then
      echo "Launched Logic Pro for AU rescan."
    else
      echo "Could not launch Logic Pro automatically."
      echo "Open Logic Pro manually to trigger rescan."
    fi
  fi
fi

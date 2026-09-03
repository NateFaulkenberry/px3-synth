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

# EVERY product, not the first one.
#
# This took `head -n1` of the PRODUCT_NAME lines, which was right when there was
# one product and quietly wrong from the moment there were eight: a developer
# cleaning their machine kept seven effects installed and a host that still
# listed them. The names and ids are read as pairs from the same
# px3_add_product table the build reads, so a product added there is cleaned up
# here without editing this script.
PRODUCT_NAMES=()
PRODUCT_IDS=()
while IFS=$'\t' read -r name id; do
  [[ -n "${name}" ]] || continue
  PRODUCT_NAMES+=("${name}")
  PRODUCT_IDS+=("${id}")
done < <(awk '
  /^px3_add_product\(/ { name = ""; id = ""; next }
  /^[[:space:]]*PRODUCT_NAME[[:space:]]+"/ {
    if (match($0, /"[^"]*"/)) { name = substr($0, RSTART + 1, RLENGTH - 2) }
    next
  }
  /^[[:space:]]*BUNDLE_ID[[:space:]]+"/ {
    if (match($0, /"[^"]*"/)) { id = substr($0, RSTART + 1, RLENGTH - 2) }
    next
  }
  /\)[[:space:]]*$/ { if (name != "") { print name "\t" id }; name = ""; id = "" }
' "${CMAKE_FILE}")

if [[ ${#PRODUCT_NAMES[@]} -eq 0 ]]; then
  PRODUCT_NAMES=("PX3 Synth")
  PRODUCT_IDS=("com.px3.px3synth")
fi

echo "Removing ${#PRODUCT_NAMES[@]} product(s): ${PRODUCT_NAMES[*]}"
echo ""

removed_any=false
permission_blocked=false
matches_found=false

# The shared paths only. Everything keyed by a product's name or bundle id is
# built inside the per-product loop, where those two are actually in scope -
# under `set -u` a ${BUNDLE_ID} up here is now an abort, not an empty string.
USER_APP_SUPPORT_PATH="${HOME}/Library/Application Support/P(X3)"
SYSTEM_APP_SUPPORT_PATH="/Library/Application Support/P(X3)"

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

for i in "${!PRODUCT_NAMES[@]}"; do
  PRODUCT_NAME="${PRODUCT_NAMES[$i]}"
  BUNDLE_ID="${PRODUCT_IDS[$i]}"

  echo "--- ${PRODUCT_NAME}"

  remove_path_if_present "${HOME}/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component" "User AU"
  remove_path_if_present "${HOME}/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3" "User VST3"
  remove_path_if_present "/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component" "System AU"
  remove_path_if_present "/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3" "System VST3"
  remove_path_if_present "/Applications/${PRODUCT_NAME}.app" "Standalone App"

  remove_path_if_present "${HOME}/Library/Logs/${PRODUCT_NAME}" "User Log Folder"
  remove_glob_if_present "${HOME}/Library/Logs/DiagnosticReports/${PRODUCT_NAME}-*.ips" "User Crash Report"
  remove_glob_if_present "/Library/Logs/DiagnosticReports/${PRODUCT_NAME}-*.ips" "System Crash Report"

  # A product with no BUNDLE_ID in the table has no id-keyed paths to remove,
  # and guessing one would risk deleting something belonging to another product.
  if [[ -n "${BUNDLE_ID}" ]]; then
    remove_path_if_present "${HOME}/Library/Preferences/${BUNDLE_ID}.plist" "User Preferences"
    remove_path_if_present "/Library/Preferences/${BUNDLE_ID}.plist" "System Preferences"
    remove_path_if_present "${HOME}/Library/Caches/${BUNDLE_ID}" "User Cache"
    remove_path_if_present "/Library/Caches/${BUNDLE_ID}" "System Cache"
    remove_path_if_present "${HOME}/Library/Saved Application State/${BUNDLE_ID}.savedState" "User Saved State"
    remove_path_if_present "${HOME}/Library/Logs/${BUNDLE_ID}" "User Log Folder"
  fi

  echo ""
done

# Shared across every product, so it is removed once rather than per product.
# This is a developer-machine reset: unlike the shipped uninstaller it takes the
# preset library without asking, which is the point of it.
remove_path_if_present "${HOME}/Library/P(X3)" "User Preset Library and Settings"
remove_path_if_present "${USER_APP_SUPPORT_PATH}" "User App Support"
remove_path_if_present "${SYSTEM_APP_SUPPORT_PATH}" "System App Support"

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

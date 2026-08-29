#!/bin/sh
# PX3 removal. Single source of truth - used by the uninstaller application.
#
# Must be run as root. It walks every home directory under /Users rather than
# using $HOME, because presets live per-user and this runs as root, whose $HOME
# is not the person uninstalling.
#
# Identity comes from the environment so the script is standalone and testable;
# scripts/build-release.sh passes the values it read from CMakeLists.txt.
# Identity written alongside this script at build time, so the values match the
# build that produced it rather than being guessed at here.
SELF_DIR=$(CD=$(dirname "$0"); cd "$CD" && pwd)
if [ -f "${SELF_DIR}/px3-identity.env" ]; then
    . "${SELF_DIR}/px3-identity.env"
fi

PRODUCT_NAME="${PX3_PRODUCT_NAME:-PX3 Synth}"
BUNDLE_ID="${PX3_BUNDLE_ID:-com.px3.px3synth}"
APP_SUPPORT_NAME="${PX3_APP_SUPPORT_NAME:-P(X3)}"
AU_PACKAGE_ID="${PX3_AU_PACKAGE_ID:-${BUNDLE_ID}.au}"
VST3_PACKAGE_ID="${PX3_VST3_PACKAGE_ID:-${BUNDLE_ID}.vst3}"
UNINSTALLER_PACKAGE_ID="${PX3_UNINSTALLER_PACKAGE_ID:-${BUNDLE_ID}.uninstaller}"
APP_PACKAGE_ID="${PX3_APP_PACKAGE_ID:-${BUNDLE_ID}.standalone}"

# A dry run prints what it would remove and changes nothing. Used by the tests.
PX3_DRY_RUN="${PX3_DRY_RUN:-0}"

# Deliberately no `set -e`: one unreadable path must not abort the rest of the
# removal, or the uninstall silently finishes half done.
set -u

LOG_FILE="${PX3_LOG_FILE:-/tmp/px3-uninstall.log}"
# A dry run reports to stdout so it can be inspected; a real run logs to file.
if [ "${PX3_DRY_RUN}" != "1" ]; then
    exec >>"${LOG_FILE}" 2>&1
fi
echo "===== P(X3) uninstall $(date) ====="

removed_count=0

# Refuses to act on an empty or dangerously broad path. Every path below is
# built from variables, and a variable that came out empty would otherwise turn
# "rm -rf ${root}/Library/..." into something catastrophic.
safe_remove() {
  local path="$1"
  local label="$2"

  case "${path}" in
    ""|"/"|"/Users"|"/Library"|"/Applications"|"/System"|"/private"|"/var"|"/usr"|"/bin"|"/etc")
      echo "REFUSED (unsafe path) ${label}: '${path}'"
      return 0
      ;;
  esac
  case "${path}" in
    */) echo "REFUSED (trailing slash) ${label}: '${path}'"; return 0 ;;
    *"//"*) echo "REFUSED (malformed path) ${label}: '${path}'"; return 0 ;;
  esac
  if [ "${#path}" -lt 8 ]; then
    echo "REFUSED (suspiciously short) ${label}: '${path}'"
    return 0
  fi

  if [ -e "${path}" ] || [ -L "${path}" ]; then
    if [ "${PX3_DRY_RUN}" = "1" ]; then
      echo "WOULD REMOVE ${label}: ${path}"
      removed_count=$((removed_count + 1))
      return 0
    fi
    if rm -rf "${path}"; then
      echo "Removed ${label}: ${path}"
      removed_count=$((removed_count + 1))
    else
      echo "FAILED  ${label}: ${path}"
    fi
  fi
}

safe_remove_glob() {
  local pattern="$1"
  local label="$2"
  local match
  for match in ${pattern}; do
    [ -e "${match}" ] || continue
    safe_remove "${match}" "${label}"
  done
}

AU_NAME="${PRODUCT_NAME}.component"
VST3_NAME="${PRODUCT_NAME}.vst3"
APP_NAME="${PRODUCT_NAME}.app"

# ---------------------------------------------------------------------------
# 1. System-wide plugin bundles and the standalone app
# ---------------------------------------------------------------------------
safe_remove "/Library/Audio/Plug-Ins/Components/${AU_NAME}" "System AU"
safe_remove "/Library/Audio/Plug-Ins/VST3/${VST3_NAME}" "System VST3"
safe_remove "/Applications/${APP_NAME}" "Standalone app"
safe_remove "/Library/${APP_SUPPORT_NAME}" "System app support"
safe_remove "/Library/Application Support/${APP_SUPPORT_NAME}" "System app support (Application Support)"
safe_remove "/Library/Preferences/${BUNDLE_ID}.plist" "System preferences"
safe_remove "/Library/Caches/${BUNDLE_ID}" "System cache"
safe_remove_glob "/Library/Audio/Presets/PX3/*" "System audio preset"

# ---------------------------------------------------------------------------
# 2. Per-user data, for every user on the machine
#
# Presets live under each user's home directory, and this script runs as root,
# so the home directories have to be walked explicitly - $HOME here is root's,
# not the person who is uninstalling.
# ---------------------------------------------------------------------------
user_home_dirs() {
  local dir
  for dir in /Users/*; do
    [ -d "${dir}/Library" ] || continue
    case "$(basename "${dir}")" in
      Shared|Guest|.*) continue ;;
    esac
    echo "${dir}"
  done
  [ -d "/var/root/Library" ] && echo "/var/root"
}

for USER_HOME in $(user_home_dirs); do
  echo "--- user: ${USER_HOME}"

  safe_remove "${USER_HOME}/Library/Audio/Plug-Ins/Components/${AU_NAME}" "User AU"
  safe_remove "${USER_HOME}/Library/Audio/Plug-Ins/VST3/${VST3_NAME}" "User VST3"

  # Presets, both factory and user, plus favourites and settings. This is the
  # whole preset library and it does not come back.
  #
  # ~/Library/<name> is where the plug-in ACTUALLY writes: PresetManager uses
  # juce::File::userApplicationDataDirectory, which on macOS is ~/Library and
  # not ~/Library/Application Support. Only the Application Support path was
  # listed here, so the uninstaller reported success while leaving the entire
  # preset library in place - after a dialog that promised to remove it.
  safe_remove "${USER_HOME}/Library/${APP_SUPPORT_NAME}" "User preset library and settings"
  # The conventional location too. Nothing writes here today, but an earlier or
  # later build might, and removing a directory that is not there costs nothing.
  safe_remove "${USER_HOME}/Library/Application Support/${APP_SUPPORT_NAME}" "User preset library and settings (Application Support)"

  safe_remove "${USER_HOME}/Library/Preferences/${BUNDLE_ID}.plist" "User preferences"
  safe_remove "${USER_HOME}/Library/Caches/${BUNDLE_ID}" "User cache"
  safe_remove "${USER_HOME}/Library/Saved Application State/${BUNDLE_ID}.savedState" "User saved state"
  safe_remove "${USER_HOME}/Library/Logs/${PRODUCT_NAME}" "User log folder"
  safe_remove "${USER_HOME}/Library/Logs/${BUNDLE_ID}" "User log folder"
  safe_remove_glob "${USER_HOME}/Library/Logs/DiagnosticReports/${PRODUCT_NAME}-*.ips" "User crash report"
  safe_remove_glob "${USER_HOME}/Library/Audio/Presets/PX3/*" "User audio preset"

  # Audio Unit caches, so the plugin disappears from host plugin managers
  # instead of lingering as a broken entry until the next rescan.
  safe_remove_glob "${USER_HOME}/Library/Caches/AudioUnitCache/com.apple.audiounits*" "User AudioUnit cache"
  safe_remove_glob "${USER_HOME}/Library/Caches/com.apple.logic10/*AudioUnit*" "Logic AudioUnit cache"
  safe_remove_glob "${USER_HOME}/Library/Caches/com.apple.logic.pro/*AudioUnit*" "Logic Pro AudioUnit cache"
done

safe_remove_glob "/Library/Caches/AudioUnitCache/com.apple.audiounits*" "System AudioUnit cache"
safe_remove_glob "/Library/Logs/DiagnosticReports/${PRODUCT_NAME}-*.ips" "System crash report"

# ---------------------------------------------------------------------------
# 3. Installer receipts
#
# Without this the system still believes the plugin is installed, and a later
# installer run can decide it has nothing to do.
# ---------------------------------------------------------------------------
for receipt in "${AU_PACKAGE_ID}" "${VST3_PACKAGE_ID}" "${UNINSTALLER_PACKAGE_ID}"; do
  if pkgutil --pkgs | grep -Fxq "${receipt}"; then
    if [ "${PX3_DRY_RUN}" = "1" ]; then
      echo "WOULD forget receipt: ${receipt}"
    else
      pkgutil --forget "${receipt}" >/dev/null 2>&1 && echo "Forgot receipt: ${receipt}"
    fi
  fi
done

# Restart the component registrar so the AU disappears without a reboot.
if [ "${PX3_DRY_RUN}" != "1" ]; then
    pkill -x AudioComponentRegistrar >/dev/null 2>&1 && echo "Restarted AudioComponentRegistrar."
fi

echo "Uninstall complete. ${removed_count} item(s) removed."
echo "Quit and reopen any host application to refresh its plugin list."
exit 0

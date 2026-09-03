#!/bin/sh
# PX3 removal. Single source of truth - used by the uninstaller application.
#
# Must be run as root. It walks every home directory under /Users rather than
# using $HOME, because presets live per-user and this runs as root, whose $HOME
# is not the person uninstalling.
#
# ---------------------------------------------------------------------------
# What it removes is chosen by the caller, not baked in
# ---------------------------------------------------------------------------
#
#   PX3_PRODUCTS      Newline- or "|"-separated product names to remove, e.g.
#                     "PX3 Synth|PX3 Mood". Required. There is deliberately no
#                     "everything" default: an uninstaller that removes more
#                     than the user selected because a variable came out empty
#                     is the worst bug this script could have.
#
#   PX3_KEEP_PRESETS  1 (default) keeps the user's own presets and imported
#                     wavetables so a later reinstall finds them. 0 removes the
#                     whole shared data directory - user presets, factory
#                     presets, settings and wavetables together.
#
#   PX3_DRY_RUN       1 prints what it would remove and changes nothing.
#
#   PX3_SCAN_ROOT     Prefix every system path with this. Empty in production;
#                     the tests point it at a fixture tree so the branches that
#                     depend on what is still installed - which is most of the
#                     interesting ones - can be exercised without a machine
#                     that happens to have the right products on it.
#
# Identity comes from the environment so the script is standalone and testable;
# scripts/build-release.sh passes the values it read from CMakeLists.txt.
SELF_DIR=$(CD=$(dirname "$0"); cd "$CD" && pwd)
if [ -f "${SELF_DIR}/px3-identity.env" ]; then
    . "${SELF_DIR}/px3-identity.env"
fi

MANIFEST="${PX3_MANIFEST:-${SELF_DIR}/px3-products.tsv}"

# The shared data directory. It belongs to the ecosystem rather than to any one
# product, which is why it is removed on its own terms further down and not as
# part of a product.
APP_SUPPORT_NAME="${PX3_APP_SUPPORT_NAME:-P(X3)}"

PX3_KEEP_PRESETS="${PX3_KEEP_PRESETS:-1}"
PX3_DRY_RUN="${PX3_DRY_RUN:-0}"
PX3_PRODUCTS="${PX3_PRODUCTS:-}"
ROOT="${PX3_SCAN_ROOT:-}"

# Deliberately no `set -e`: one unreadable path must not abort the rest of the
# removal, or the uninstall silently finishes half done.
set -u

LOG_FILE="${PX3_LOG_FILE:-/tmp/px3-uninstall.log}"
# A dry run reports to stdout so it can be inspected; a real run logs to file.
#
# The log is tested for writability first. `exec >>` on a file this process
# cannot open is a fatal redirection error, which killed the entire uninstall
# before it removed anything - and a stale /tmp/px3-uninstall.log owned by root
# from a previous run is exactly how that happens. A log nobody can write is a
# reason to carry on without one, not a reason to stop.
if [ "${PX3_DRY_RUN}" != "1" ]; then
    if : >>"${LOG_FILE}" 2>/dev/null; then
        exec >>"${LOG_FILE}" 2>&1
    else
        echo "WARNING: cannot write ${LOG_FILE}; continuing without a log file" >&2
    fi
fi
echo "===== P(X3) uninstall $(date) ====="

if [ -z "${PX3_PRODUCTS}" ]; then
  echo "ERROR: no products selected (PX3_PRODUCTS is empty). Nothing removed."
  exit 2
fi

removed_count=0

# Refuses to act on an empty or dangerously broad path. Every path below is
# built from variables, and a variable that came out empty would otherwise turn
# "rm -rf ${root}/Library/..." into something catastrophic.
safe_remove() {
  local path="$1"
  local label="$2"

  # Judged on the path WITHOUT the test prefix, so a fixture root cannot make
  # a dangerous path look safe merely by being longer.
  local bare="${path}"
  if [ -n "${ROOT}" ]; then bare="${path#"${ROOT}"}"; fi

  case "${bare}" in
    ""|"/"|"/Users"|"/Library"|"/Applications"|"/System"|"/private"|"/var"|"/usr"|"/bin"|"/etc")
      echo "REFUSED (unsafe path) ${label}: '${path}'"
      return 0
      ;;
  esac
  case "${path}" in
    */) echo "REFUSED (trailing slash) ${label}: '${path}'"; return 0 ;;
    *"//"*) echo "REFUSED (malformed path) ${label}: '${path}'"; return 0 ;;
  esac
  if [ "${#bare}" -lt 8 ]; then
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

# Everything below this point that reaches OUTSIDE the path tree - the package
# receipt database, the running component registrar - is real-machine state
# that a scan root cannot redirect. A run against a fixture must not touch it:
# the tests select "PX3 Synth", and forgetting com.px3.px3synth.au would forget
# the receipt for the copy actually installed on the developer's machine.
LIVE_SYSTEM=1
if [ -n "${ROOT}" ]; then LIVE_SYSTEM=0; fi

# The installed receipt list, read once.
#
# forget_receipt used to run `pkgutil --pkgs` per candidate, and there are five
# candidates per product: removing all eight products meant forty full package
# enumerations, which is slow enough to look like a hang.
INSTALLED_RECEIPTS=""
if [ "${LIVE_SYSTEM}" = "1" ]; then
  INSTALLED_RECEIPTS=$(pkgutil --pkgs 2>/dev/null || true)
fi

forget_receipt() {
  local receipt="$1"
  [ -n "${receipt}" ] || return 0
  [ "${LIVE_SYSTEM}" = "1" ] || { echo "SKIPPED (fixture run) receipt: ${receipt}"; return 0; }
  if printf '%s\n' "${INSTALLED_RECEIPTS}" | grep -Fxq "${receipt}"; then
    if [ "${PX3_DRY_RUN}" = "1" ]; then
      echo "WOULD forget receipt: ${receipt}"
    else
      pkgutil --forget "${receipt}" >/dev/null 2>&1 && echo "Forgot receipt: ${receipt}"
    fi
  fi
}

user_home_dirs() {
  local dir
  for dir in "${ROOT}"/Users/*; do
    [ -d "${dir}/Library" ] || continue
    case "$(basename "${dir}")" in
      Shared|Guest|.*) continue ;;
    esac
    echo "${dir}"
  done
  [ -d "${ROOT}/var/root/Library" ] && echo "${ROOT}/var/root"
}

USER_HOMES=$(user_home_dirs)

# The bundle id for a product name, from the manifest. A product the manifest
# does not list still gets its bundles removed - only the receipt and the
# preference file need an id, and guessing one would risk forgetting a receipt
# that belongs to something else.
bundle_id_for() {
  [ -f "${MANIFEST}" ] || return 0
  awk -F'\t' -v n="$1" '$2 == n { print $3; exit }' "${MANIFEST}"
}

# ---------------------------------------------------------------------------
# 1. Each selected product
# ---------------------------------------------------------------------------
#
# Only the products the user chose. Removing PX3 Mood must leave PX3 Synth
# working, so nothing here touches anything shared.
OLD_IFS="${IFS}"
IFS='|
'
for PRODUCT_NAME in ${PX3_PRODUCTS}; do
  IFS="${OLD_IFS}"
  [ -n "${PRODUCT_NAME}" ] || continue

  echo "--- product: ${PRODUCT_NAME}"

  AU_NAME="${PRODUCT_NAME}.component"
  VST3_NAME="${PRODUCT_NAME}.vst3"
  APP_NAME="${PRODUCT_NAME}.app"
  PRODUCT_BUNDLE_ID=$(bundle_id_for "${PRODUCT_NAME}")

  safe_remove "${ROOT}/Library/Audio/Plug-Ins/Components/${AU_NAME}" "System AU"
  safe_remove "${ROOT}/Library/Audio/Plug-Ins/VST3/${VST3_NAME}" "System VST3"
  safe_remove "${ROOT}/Applications/${APP_NAME}" "Standalone app"

  for USER_HOME in ${USER_HOMES}; do
    safe_remove "${USER_HOME}/Library/Audio/Plug-Ins/Components/${AU_NAME}" "User AU"
    safe_remove "${USER_HOME}/Library/Audio/Plug-Ins/VST3/${VST3_NAME}" "User VST3"
    safe_remove "${USER_HOME}/Library/Saved Application State/${PRODUCT_BUNDLE_ID}.savedState" "User saved state"
    safe_remove "${USER_HOME}/Library/Logs/${PRODUCT_NAME}" "User log folder"
    safe_remove_glob "${USER_HOME}/Library/Logs/DiagnosticReports/${PRODUCT_NAME}-*.ips" "User crash report"

    if [ -n "${PRODUCT_BUNDLE_ID}" ]; then
      safe_remove "${USER_HOME}/Library/Preferences/${PRODUCT_BUNDLE_ID}.plist" "User preferences"
      safe_remove "${USER_HOME}/Library/Caches/${PRODUCT_BUNDLE_ID}" "User cache"
      safe_remove "${USER_HOME}/Library/Logs/${PRODUCT_BUNDLE_ID}" "User log folder"
    fi
  done

  safe_remove_glob "${ROOT}/Library/Logs/DiagnosticReports/${PRODUCT_NAME}-*.ips" "System crash report"

  if [ -n "${PRODUCT_BUNDLE_ID}" ]; then
    safe_remove "${ROOT}/Library/Preferences/${PRODUCT_BUNDLE_ID}.plist" "System preferences"
    safe_remove "${ROOT}/Library/Caches/${PRODUCT_BUNDLE_ID}" "System cache"

    # Without this the system still believes the plug-in is installed, and a
    # later installer run can decide it has nothing to do.
    for suffix in au vst3 standalone uninstaller; do
      forget_receipt "${PRODUCT_BUNDLE_ID}.${suffix}"
    done
    forget_receipt "${PRODUCT_BUNDLE_ID}"
  fi

  IFS='|
'
done
IFS="${OLD_IFS}"

# ---------------------------------------------------------------------------
# 2. The shared P(X3) data directory
# ---------------------------------------------------------------------------
#
# Presets, wavetables and settings live in one directory shared by every PX3
# product. Two rules govern it, and both matter:
#
#   - It is only touched when NO PX3 product is left installed afterwards.
#     Removing PX3 Mood must not delete the Synth's preset library.
#   - What goes then depends on the user's choice. Keeping presets keeps the
#     things they made - their own presets and any wavetables they imported -
#     and drops what a reinstall puts back anyway.
remaining_products() {
  # Anything still on disk after the removals above.
  for dir in "${ROOT}/Library/Audio/Plug-Ins/Components" "${ROOT}/Library/Audio/Plug-Ins/VST3"; do
    for bundle in "${dir}"/PX3\ * "${dir}"/P\(X3\)*; do
      [ -e "${bundle}" ] && return 0
    done
  done
  for USER_HOME in ${USER_HOMES}; do
    for dir in "${USER_HOME}/Library/Audio/Plug-Ins/Components" "${USER_HOME}/Library/Audio/Plug-Ins/VST3"; do
      for bundle in "${dir}"/PX3\ * "${dir}"/P\(X3\)*; do
        [ -e "${bundle}" ] && return 0
      done
    done
  done
  for app in "${ROOT}"/Applications/PX3\ *.app; do
    [ -e "${app}" ] && return 0
  done
  return 1
}

if remaining_products; then
  echo "--- shared data kept: other PX3 products are still installed"
else
  for USER_HOME in ${USER_HOMES}; do
    SHARED_DIR="${USER_HOME}/Library/${APP_SUPPORT_NAME}"
    ALT_SHARED_DIR="${USER_HOME}/Library/Application Support/${APP_SUPPORT_NAME}"

    if [ "${PX3_KEEP_PRESETS}" = "1" ]; then
      echo "--- keeping presets and imported wavetables in ${SHARED_DIR}"
      # Everything a reinstall puts back. What the user made stays.
      safe_remove "${SHARED_DIR}/Presets/Factory" "Factory presets"
      safe_remove "${SHARED_DIR}/Settings" "Settings folder"
      safe_remove "${SHARED_DIR}/Updates" "Staged updates"
      safe_remove "${SHARED_DIR}/settings.xml" "Settings file"
    else
      echo "--- removing ALL presets, wavetables and settings in ${SHARED_DIR}"
      safe_remove "${SHARED_DIR}" "User preset library, wavetables and settings"
      safe_remove "${ALT_SHARED_DIR}" "User preset library (Application Support)"
    fi
  done

  if [ "${PX3_KEEP_PRESETS}" != "1" ]; then
    safe_remove "${ROOT}/Library/${APP_SUPPORT_NAME}" "System app support"
    safe_remove "${ROOT}/Library/Application Support/${APP_SUPPORT_NAME}" "System app support (Application Support)"
    safe_remove_glob "${ROOT}/Library/Audio/Presets/PX3/*" "System audio preset"
    for USER_HOME in ${USER_HOMES}; do
      safe_remove_glob "${USER_HOME}/Library/Audio/Presets/PX3/*" "User audio preset"
    done
  fi
fi

# ---------------------------------------------------------------------------
# 3. Audio Unit caches, so removed plug-ins disappear from host plug-in
#    managers instead of lingering as broken entries until the next rescan.
# ---------------------------------------------------------------------------
for USER_HOME in ${USER_HOMES}; do
  safe_remove_glob "${USER_HOME}/Library/Caches/AudioUnitCache/com.apple.audiounits*" "User AudioUnit cache"
  safe_remove_glob "${USER_HOME}/Library/Caches/com.apple.logic10/*AudioUnit*" "Logic AudioUnit cache"
  safe_remove_glob "${USER_HOME}/Library/Caches/com.apple.logic.pro/*AudioUnit*" "Logic Pro AudioUnit cache"
done
safe_remove_glob "${ROOT}/Library/Caches/AudioUnitCache/com.apple.audiounits*" "System AudioUnit cache"

# Restart the component registrar so the AU disappears without a reboot.
if [ "${PX3_DRY_RUN}" != "1" ] && [ "${LIVE_SYSTEM}" = "1" ]; then
    pkill -x AudioComponentRegistrar >/dev/null 2>&1 && echo "Restarted AudioComponentRegistrar."
fi

echo "Uninstall complete. ${removed_count} item(s) removed."
echo "Quit and reopen any host application to refresh its plugin list."
exit 0

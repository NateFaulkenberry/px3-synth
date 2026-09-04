#!/bin/bash
# Exercise the updater helper end to end without publishing a release.
#
# The update path has now failed twice in ways nothing caught, and both times
# the reason was that no one had ever watched the helper run against a real
# installer:
#
#   1. The helper was launched with its stdout on a pipe the plug-in closed,
#      so it died on its first write - before it could log why.
#   2. The signature check built one command string with quotes in it. There is
#      no shell, so pkgutil received literal quote characters, could not open
#      the file, and every correctly signed installer was refused as unsigned.
#
# Neither is visible from the plug-in. Both are obvious here.
#
# Usage:
#   scripts/test-updater-locally.sh <installer.pkg> [--run]
#
# Without --install it stops after the checks the helper makes and reports what
# it decided, changing nothing on the machine. With --run it goes through
# with it, which really does run Installer.app.

set -uo pipefail

STAGING="${HOME}/Library/P(X3)/Updates"
LOG="${STAGING}/updater.log"

PKG="${1:-}"
MODE="${2:-}"

die() { echo "error: $*" >&2; exit 1; }

[ -n "${PKG}" ] || die "usage: $0 <installer.pkg> [--run]"
[ -f "${PKG}" ] || die "no such file: ${PKG}"
case "${PKG}" in *.pkg) ;; *) die "not a .pkg: ${PKG}" ;; esac

HELPER="$(find "$(pwd)" -name "PX3 Updater" -type f -perm -u+x -path "*Standalone*" 2>/dev/null | head -1)"
[ -n "${HELPER}" ] || HELPER="$(find "$(pwd)/build" -name "PX3Updater" -type f -perm -u+x 2>/dev/null | head -1)"
[ -n "${HELPER}" ] || die "no built updater found - build PX3Updater or the standalone first"

echo "helper:     ${HELPER}"
echo "installer:  ${PKG}"
echo "staging:    ${STAGING}"
echo

# ---- what the helper will check, checked here first ------------------------
#
# Reported rather than assumed, so a refusal is explained by this script rather
# than only by the helper's log.
echo "== the three things the helper requires =="

mkdir -p "${STAGING}"
STAGED="${STAGING}/$(basename "${PKG}")"
if [ "${PKG}" != "${STAGED}" ]; then
  cp "${PKG}" "${STAGED}"
fi
echo "  in staging directory: yes (${STAGED})"
echo "  is a .pkg:            yes"

SIG="$(/usr/sbin/pkgutil --check-signature "${STAGED}" 2>&1)"
if printf '%s' "${SIG}" | grep -q "Developer ID Installer:"; then
  echo "  Developer ID signed:  yes"
  printf '%s\n' "${SIG}" | sed -n '2,3p' | sed 's/^/      /'
else
  echo "  Developer ID signed:  NO - the helper will refuse this installer"
  printf '%s\n' "${SIG}" | head -3 | sed 's/^/      /'
  echo
  echo "  An unsigned local build cannot be installed by the updater, by design."
  echo "  Build with --sign to test the real path:  ./scripts/build-release.sh --sign"
fi
echo

if [ "${MODE}" != "--run" ]; then
  echo "Stopping here. Pass --run to let the helper actually run it."
  echo "(that hands the package to Installer.app and asks for your password)"
  exit 0
fi

# ---- run it the way the plug-in does ---------------------------------------
#
# Same arguments, and crucially the same stdio: the plug-in hands the helper
# /dev/null and exits. Reproducing that is the whole point - run it attached to
# a terminal and the bug that shipped in v0.7.1 does not occur.
echo "== running the helper as the plug-in launches it =="
: > "${LOG}"

"${HELPER}" --install "${STAGED}" --wait-for-pid $$ >/dev/null 2>&1 &
HELPER_PID=$!
echo "  started pid ${HELPER_PID}, detached from this terminal"

# It waits for the pid it was given, which here is this script. In the real
# flow that is the DAW. Give it a moment to get through its checks.
sleep 3

echo
echo "== updater.log =="
[ -s "${LOG}" ] && sed 's/^/  /' "${LOG}" || echo "  EMPTY - the helper died before logging anything"

echo
echo "This script is still running, so the helper is still waiting on it."
echo "It installs when this exits. Press Ctrl-C to abandon, or Enter to go ahead."
read -r _

wait "${HELPER_PID}" 2>/dev/null
echo
echo "== updater.log after =="
sed 's/^/  /' "${LOG}" 2>/dev/null || echo "  (no log)"

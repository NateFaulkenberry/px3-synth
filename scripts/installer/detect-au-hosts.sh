#!/bin/sh
# Reports which known macOS Audio Unit hosts are currently running.
#
# Identity comes from BUNDLE IDENTIFIERS, read from each running application's
# own Info.plist, matched against scripts/installer/au-hosts.tsv. It is not a
# process-name search: names like "Live", "Reason" and "LUNA" are ordinary
# words, and name matching is what previously blocked the installer whenever
# macOS ran `auval` after an Audio Unit install.
#
# Only processes running from inside a .app bundle are considered, which
# excludes every daemon and system service by construction.
#
# Usage:
#   detect-au-hosts.sh              print running hosts, one per line
#                                   exit 0 = none, 1 = at least one
#   detect-au-hosts.sh --list       print every running application and its
#                                   bundle identifier (diagnostic)
#   detect-au-hosts.sh --database   print the host database being used
#
# Testing hook: set PX3_FAKE_BUNDLE_IDS to a comma or newline separated list to
# test classification without launching applications.

set -u

SELF_DIR=$(CD=$(dirname "$0"); cd "$CD" && pwd)
DB="${PX3_AU_HOST_DB:-${SELF_DIR}/au-hosts.tsv}"

if [ ! -f "${DB}" ]; then
    # Without the database nothing can be classified. Fail OPEN rather than
    # closed: blocking every install because a data file is missing is a worse
    # outcome than not warning about a running host, and a missing database is
    # a packaging fault that the build script verifies against separately.
    echo "detect-au-hosts: host database not found at ${DB}" >&2
    exit 0
fi

# --- bundle identifiers of everything currently running -------------------
#
# Two independent sources, unioned. lsappinfo asks LaunchServices, which knows
# about GUI applications in the user's session; walking the process table and
# reading Info.plist works even when the caller is root in the installer's own
# context, where LaunchServices may not see the user session at all.
running_bundle_ids() {
    if [ -n "${PX3_FAKE_BUNDLE_IDS:-}" ]; then
        printf '%s\n' "${PX3_FAKE_BUNDLE_IDS}" | tr ',' '\n' | sed 's/^ *//;s/ *$//' | grep -v '^$'
        return
    fi

    ps -Ao args= 2>/dev/null \
        | grep -oE '^/[^ ]*\.app/Contents/MacOS/[^ ]*' \
        | sed 's|/Contents/MacOS/.*||' \
        | sort -u \
        | while IFS= read -r app; do
            [ -f "${app}/Contents/Info.plist" ] || continue
            /usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" \
                "${app}/Contents/Info.plist" 2>/dev/null
          done

    if [ -x /usr/bin/lsappinfo ]; then
        /usr/bin/lsappinfo list 2>/dev/null \
            | grep -oE 'bundleID="[^"]*"' \
            | sed 's/bundleID="//;s/"$//'
    fi
}

# --- diagnostics ----------------------------------------------------------
if [ "${1:-}" = "--list" ]; then
    echo "Running applications:"
    echo
    ps -Ao args= 2>/dev/null \
        | grep -oE '^/[^ ]*\.app/Contents/MacOS/[^ ]*' \
        | sed 's|/Contents/MacOS/.*||' \
        | sort -u \
        | while IFS= read -r app; do
            name=$(basename "${app}" .app)
            bid=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" \
                  "${app}/Contents/Info.plist" 2>/dev/null)
            printf '%s\n    %s\n' "${name}" "${bid:-<no bundle identifier>}"
          done
    exit 0
fi

if [ "${1:-}" = "--database" ]; then
    grep -vE '^[[:space:]]*(#|$)' "${DB}"
    exit 0
fi

# --- classification -------------------------------------------------------
RUNNING=$(running_bundle_ids | tr '[:upper:]' '[:lower:]' | sort -u)
[ -n "${RUNNING}" ] || exit 0

FOUND=""
# Read the database with a file descriptor so the loop body keeps the parent
# shell's variables - a pipe would run it in a subshell and FOUND would be lost.
while IFS="$(printf '\t')" read -r displayName formats bundleIds verification || [ -n "${displayName}" ]; do
    case "${displayName}" in
        ''|'#'*) continue ;;
    esac
    [ -n "${bundleIds:-}" ] || continue

    printf '%s\n' "${bundleIds}" | tr ',' '\n' | while IFS= read -r id; do
        [ -n "${id}" ] || continue
        lower=$(printf '%s' "${id}" | tr '[:upper:]' '[:lower:]')
        if printf '%s\n' "${RUNNING}" | grep -qxF "${lower}"; then
            printf '%s\n' "${displayName}"
            break
        fi
    done
    unset formats verification
done < "${DB}" | sort -u > /tmp/px3-running-hosts.$$

FOUND=$(cat /tmp/px3-running-hosts.$$ 2>/dev/null)
rm -f /tmp/px3-running-hosts.$$

if [ -z "${FOUND}" ]; then
    exit 0
fi

printf '%s\n' "${FOUND}"
exit 1

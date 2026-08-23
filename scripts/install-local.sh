#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

INSTALL_VST3=true

usage() {
  cat <<'EOF'
Usage:
  ./scripts/install-local.sh [--au-only]

Installs the most recent Release AU build into:
  ~/Library/Audio/Plug-Ins/Components/

By default, also installs VST3 into:
  ~/Library/Audio/Plug-Ins/VST3/
EOF
}

die() {
  echo "ERROR: $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --au-only)
      INSTALL_VST3=false
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

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

require_cmd find
require_cmd ls

mapfile -t AU_CANDIDATES < <(find "${REPO_ROOT}/build/release" -type d -name "*.component" 2>/dev/null)
[[ ${#AU_CANDIDATES[@]} -gt 0 ]] || die "No Release AU bundle found under build/release. Run ./scripts/build-release.sh first."

AU_BUNDLE="$(ls -td "${AU_CANDIDATES[@]}" | head -n1)"
AU_NAME="$(basename "${AU_BUNDLE}")"

LOCAL_AU_DIR="${HOME}/Library/Audio/Plug-Ins/Components"
LOCAL_AU_PATH="${LOCAL_AU_DIR}/${AU_NAME}"

mkdir -p "${LOCAL_AU_DIR}"
rm -rf "${LOCAL_AU_PATH}"
cp -R "${AU_BUNDLE}" "${LOCAL_AU_PATH}"

echo "Installed AU:"
echo "  Source:      ${AU_BUNDLE}"
echo "  Destination: ${LOCAL_AU_PATH}"

if [[ "${INSTALL_VST3}" == true ]]; then
  mapfile -t VST3_CANDIDATES < <(find "${REPO_ROOT}/build/release" -type d -name "*.vst3" 2>/dev/null)
  if [[ ${#VST3_CANDIDATES[@]} -gt 0 ]]; then
    VST3_BUNDLE="$(ls -td "${VST3_CANDIDATES[@]}" | head -n1)"
    VST3_NAME="$(basename "${VST3_BUNDLE}")"

    LOCAL_VST3_DIR="${HOME}/Library/Audio/Plug-Ins/VST3"
    LOCAL_VST3_PATH="${LOCAL_VST3_DIR}/${VST3_NAME}"

    mkdir -p "${LOCAL_VST3_DIR}"
    rm -rf "${LOCAL_VST3_PATH}"
    cp -R "${VST3_BUNDLE}" "${LOCAL_VST3_PATH}"

    echo "Installed VST3:"
    echo "  Source:      ${VST3_BUNDLE}"
    echo "  Destination: ${LOCAL_VST3_PATH}"
  else
    echo "VST3 not found in build/release; AU installed only."
  fi
fi

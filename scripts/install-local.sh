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

Also clears previously installed factory presets from:
  ~/Library/Application Support/P(X3)/Presets/Factory/
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

LOCAL_PRESET_ROOT="${HOME}/Library/Application Support/P(X3)/Presets"
LOCAL_FACTORY_PRESET_DIR="${LOCAL_PRESET_ROOT}/Factory"

find_newest_bundle() {
  local search_root="$1"
  local extension="$2"
  local newest=""
  local candidate=""

  while IFS= read -r -d '' candidate; do
    if [[ -z "${newest}" || "${candidate}" -nt "${newest}" ]]; then
      newest="${candidate}"
    fi
  done < <(find "${search_root}" -type d -name "*.${extension}" -print0 2>/dev/null)

  printf '%s\n' "${newest}"
}

AU_BUNDLE="$(find_newest_bundle "${REPO_ROOT}/build/release" "component")"
[[ -n "${AU_BUNDLE}" ]] || die "No Release AU bundle found under build/release. Run ./scripts/build-release.sh first."
AU_NAME="$(basename "${AU_BUNDLE}")"

LOCAL_AU_DIR="${HOME}/Library/Audio/Plug-Ins/Components"
LOCAL_AU_PATH="${LOCAL_AU_DIR}/${AU_NAME}"

if [[ -d "${LOCAL_FACTORY_PRESET_DIR}" ]]; then
  rm -rf "${LOCAL_FACTORY_PRESET_DIR}"
  echo "Removed installed factory presets:"
  echo "  ${LOCAL_FACTORY_PRESET_DIR}"
fi

mkdir -p "${LOCAL_AU_DIR}"
rm -rf "${LOCAL_AU_PATH}"
cp -R "${AU_BUNDLE}" "${LOCAL_AU_PATH}"

echo "Installed AU:"
echo "  Source:      ${AU_BUNDLE}"
echo "  Destination: ${LOCAL_AU_PATH}"

if [[ "${INSTALL_VST3}" == true ]]; then
  VST3_BUNDLE="$(find_newest_bundle "${REPO_ROOT}/build/release" "vst3")"
  if [[ -n "${VST3_BUNDLE}" ]]; then
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

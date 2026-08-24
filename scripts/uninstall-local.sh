#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_FILE="${REPO_ROOT}/CMakeLists.txt"

die() {
  echo "ERROR: $*" >&2
  exit 1
}

[[ -f "${CMAKE_FILE}" ]] || die "CMakeLists.txt not found"

PRODUCT_NAME="$(grep -E '^[[:space:]]*PRODUCT_NAME[[:space:]]+"' "${CMAKE_FILE}" | head -n1 | sed -E 's/^[[:space:]]*PRODUCT_NAME[[:space:]]+"([^"]+)".*/\1/' || true)"
[[ -n "${PRODUCT_NAME}" ]] || PRODUCT_NAME="PX3 Synth"

AU_NAME="${PRODUCT_NAME}.component"
VST3_NAME="${PRODUCT_NAME}.vst3"

AU_PATH="${HOME}/Library/Audio/Plug-Ins/Components/${AU_NAME}"
VST3_PATH="${HOME}/Library/Audio/Plug-Ins/VST3/${VST3_NAME}"

removed_any=false

remove_bundle_if_present() {
  local path="$1"
  local label="$2"

  if [[ -d "${path}" ]]; then
    rm -rf "${path}"
    echo "Removed ${label}: ${path}"
    removed_any=true
  else
    echo "${label} not found (already removed): ${path}"
  fi
}

remove_bundle_if_present "${AU_PATH}" "AU"
remove_bundle_if_present "${VST3_PATH}" "VST3"

if [[ "${removed_any}" == false ]]; then
  echo "No local P(X3) plugin artifacts were removed."
fi

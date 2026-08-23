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

if [[ -d "${AU_PATH}" ]]; then
  rm -rf "${AU_PATH}"
  echo "Removed AU: ${AU_PATH}"
  removed_any=true
else
  echo "AU not found (already removed): ${AU_PATH}"
fi

if [[ -d "${VST3_PATH}" ]]; then
  rm -rf "${VST3_PATH}"
  echo "Removed VST3: ${VST3_PATH}"
  removed_any=true
else
  echo "VST3 not found (already removed): ${VST3_PATH}"
fi

if [[ "${removed_any}" == false ]]; then
  echo "No local P(X3) plugin artifacts were removed."
fi

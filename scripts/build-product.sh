#!/usr/bin/env bash
# Build one PX3 product, for development testing.
#
# The full build compiles eight products and takes minutes; working on one
# effect should not. This builds exactly one, in a build directory of its own
# so it does not fight the full build's cache, and tells you where the bundles
# went.
#
#   scripts/build-product.sh delay              AU + VST3
#   scripts/build-product.sh synth              AU + VST3 + Standalone
#   scripts/build-product.sh lucy --vst3        just the VST3, the quickest loop
#   scripts/build-product.sh doom --run         build, then open the standalone
#                                               if the product has one
#   scripts/build-product.sh --list             what there is to build
#
# COPY_PLUGIN_AFTER_BUILD is ON by default, so a successful build installs into
# ~/Library/Audio/Plug-Ins and the host sees it on its next scan. Pass
# --no-install to leave your installed plug-ins alone.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

die() { echo "ERROR: $*" >&2; exit 1; }

# Product name -> CMake target. Read from the build rather than kept here as a
# second list, so a product added with px3_add_product is buildable by this
# script without anyone remembering to update it.
declare -a TARGETS=()
while IFS= read -r line; do
  TARGETS+=("${line}")
done < <(grep -oE '^\s*TARGET\s+PX3[A-Za-z]+' CMakeLists.txt | awk '{print $2}')

list_products() {
  echo "Products in this repository:"
  for target in "${TARGETS[@]}"; do
    local short="${target#PX3}"
    printf "  %-10s (target %s)\n" "$(echo "${short}" | tr '[:upper:]' '[:lower:]')" "${target}"
  done
}

[[ $# -ge 1 ]] || { list_products; exit 0; }

if [[ "$1" == "--list" || "$1" == "-l" ]]; then list_products; exit 0; fi

WANTED="$(echo "$1" | tr '[:upper:]' '[:lower:]')"; shift

TARGET=""
for candidate in "${TARGETS[@]}"; do
  if [[ "$(echo "${candidate#PX3}" | tr '[:upper:]' '[:lower:]')" == "${WANTED}" ]]; then
    TARGET="${candidate}"
    break
  fi
done

if [[ -z "${TARGET}" ]]; then
  echo "No product called '${WANTED}'." >&2
  list_products >&2
  exit 1
fi

FORMATS=()
INSTALL=ON
RUN=false
BUILD_TYPE=RelWithDebInfo

while [[ $# -gt 0 ]]; do
  case "$1" in
    --vst3)       FORMATS+=("${TARGET}_VST3") ;;
    --au)         FORMATS+=("${TARGET}_AU") ;;
    --standalone) FORMATS+=("${TARGET}_Standalone") ;;
    --no-install) INSTALL=OFF ;;
    --run)        RUN=true ;;
    --debug)      BUILD_TYPE=Debug ;;
    *)            die "Unknown option: $1" ;;
  esac
  shift
done

# No format asked for means all of the product's formats, which is the target
# itself - JUCE makes the umbrella target depend on each format it declared.
[[ ${#FORMATS[@]} -gt 0 ]] || FORMATS=("${TARGET}")

# Its own build directory: sharing one with the full build means every switch
# between them reconfigures, which is slower than the build being saved.
BUILD_DIR="build/product-$(echo "${TARGET}" | tr '[:upper:]' '[:lower:]')"

echo "==> ${TARGET}  (${BUILD_TYPE}, install=${INSTALL})"
cmake -S . -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DPX3_COPY_PLUGIN_AFTER_BUILD="${INSTALL}" \
      > /dev/null || die "configure failed"

cmake --build "${BUILD_DIR}" --target "${FORMATS[@]}" --parallel \
  || die "build failed"

echo
echo "Built:"
ARTEFACTS="${BUILD_DIR}/${TARGET}_artefacts/${BUILD_TYPE}"
if [[ -d "${ARTEFACTS}" ]]; then
  find "${ARTEFACTS}" -maxdepth 2 \( -name '*.vst3' -o -name '*.component' -o -name '*.app' \) \
    -exec echo "  {}" \;
fi

if [[ "${INSTALL}" == "ON" ]]; then
  echo
  echo "Installed to ~/Library/Audio/Plug-Ins - rescan in your host to pick it up."
fi

if [[ "${RUN}" == true ]]; then
  APP="$(find "${ARTEFACTS}" -maxdepth 2 -name '*.app' | head -n1 || true)"
  if [[ -n "${APP}" ]]; then
    echo "Launching ${APP}"
    open "${APP}"
  else
    echo "No standalone application for this product - effects ship as AU and VST3 only."
  fi
fi

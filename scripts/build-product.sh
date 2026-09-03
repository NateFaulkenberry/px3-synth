#!/usr/bin/env bash
# Build one PX3 product, for development testing.
#
# The full build compiles eight products and takes minutes; working on one
# effect should not. This builds exactly one, in a build directory of its own
# so it does not fight the full build's cache, and tells you where the bundles
# went.
#
# The PRODUCT NAME COMES FIRST, then options:
#
#   scripts/build-product.sh delay                   AU + VST3
#   scripts/build-product.sh synth                   AU + VST3 + Standalone
#   scripts/build-product.sh synth --run             build, then launch it
#   scripts/build-product.sh synth --debug --run     with the in-plugin debug
#                                                    panel, then launch it
#   scripts/build-product.sh lucy --vst3             just the VST3, quickest loop
#   scripts/build-product.sh synth --config Debug    unoptimised, for a debugger
#   scripts/build-product.sh --list                  what there is to build
#
# --debug is the in-plugin DEBUG PANEL, matching build-release.sh. For an
# unoptimised build to step through in a debugger, that is --config Debug.
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
DEBUG_PANEL=OFF
BUILD_TYPE=RelWithDebInfo

while [[ $# -gt 0 ]]; do
  case "$1" in
    --vst3)       FORMATS+=("${TARGET}_VST3") ;;
    --au)         FORMATS+=("${TARGET}_AU") ;;
    --standalone) FORMATS+=("${TARGET}_Standalone") ;;
    --no-install) INSTALL=OFF ;;
    --run)        RUN=true ;;
    # --debug means the in-plugin DEBUG PANEL, the same as it does in
    # build-release.sh. It used to mean CMAKE_BUILD_TYPE=Debug here, so the
    # same flag meant two different things in the two scripts - which is worse
    # than either meaning on its own.
    --debug)      DEBUG_PANEL=ON ;;
    --config)     shift; [[ $# -gt 0 ]] || die "--config needs a build type"
                  BUILD_TYPE="$1" ;;
    *)            die "Unknown option: $1" ;;
  esac
  shift
done

# Its own build directory: sharing one with the full build means every switch
# between them reconfigures, which is slower than the build being saved.
BUILD_DIR="build/product-$(echo "${TARGET}" | tr '[:upper:]' '[:lower:]')"

echo "==> ${TARGET}  (${BUILD_TYPE}, debug panel=${DEBUG_PANEL}, install=${INSTALL})"
cmake -S . -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DPX3_DEBUG_PANEL="${DEBUG_PANEL}" \
      -DPX3_COPY_PLUGIN_AFTER_BUILD="${INSTALL}" \
      > /dev/null || die "configure failed"

# No format asked for means every format the product declared. That is NOT the
# bare target: juce_add_plugin makes ${TARGET} the shared-code static library,
# and ${TARGET}_All the umbrella depending on each format. Building the bare
# target compiles everything, links the .a, and produces no loadable plugin and
# no runnable app - only the bundle skeleton, which macOS then refuses to open
# with "its executable is missing".
if [[ ${#FORMATS[@]} -eq 0 ]]; then
  # Captured, not piped into grep -q: under `set -o pipefail`, grep -q closes
  # the pipe on its first match, cmake dies of SIGPIPE, and the pipeline
  # reports failure precisely when the target WAS found.
  AVAILABLE="$(cmake --build "${BUILD_DIR}" --target help 2>/dev/null || true)"
  if grep -qx "\.\.\. ${TARGET}_All" <<< "${AVAILABLE}"; then
    FORMATS=("${TARGET}_All")
  else
    # A single-format product has no _All target; ask for the format directly.
    for f in VST3 AU Standalone; do
      if grep -qx "\.\.\. ${TARGET}_${f}" <<< "${AVAILABLE}"; then
        FORMATS+=("${TARGET}_${f}")
      fi
    done
    [[ ${#FORMATS[@]} -gt 0 ]] || die "${TARGET} declares no buildable format"
  fi
fi

cmake --build "${BUILD_DIR}" --target "${FORMATS[@]}" --parallel \
  || die "build failed"

echo
echo "Built:"
ARTEFACTS="${BUILD_DIR}/${TARGET}_artefacts/${BUILD_TYPE}"
# Report only the formats this run asked for. Listing whatever sits in the
# artefacts directory credits a --vst3 run with the AU left over from the run
# before it.
for target in "${FORMATS[@]}"; do
  case "${target}" in
    *_All) subdirs=(VST3 AU Standalone) ;;
    *_VST3) subdirs=(VST3) ;;
    *_AU) subdirs=(AU) ;;
    *_Standalone) subdirs=(Standalone) ;;
    *) subdirs=(VST3 AU Standalone) ;;
  esac
  for sub in "${subdirs[@]}"; do
    [[ -d "${ARTEFACTS}/${sub}" ]] || continue
    find "${ARTEFACTS}/${sub}" -maxdepth 1 \
      \( -name '*.vst3' -o -name '*.component' -o -name '*.app' \) \
      -exec echo "  {}" \;
  done
done

if [[ "${INSTALL}" == "ON" ]]; then
  echo
  echo "Installed to ~/Library/Audio/Plug-Ins - rescan in your host to pick it up."
fi

if [[ "${RUN}" == true ]]; then
  APP="$(find "${ARTEFACTS}" -maxdepth 2 -name '*.app' | head -n1 || true)"
  # A bundle can exist with no binary in it - CMake creates the skeleton before
  # anything links into it. Opening that gets "its executable is missing" from
  # macOS, which reads like a broken build rather than a build that never ran.
  if [[ -n "${APP}" && ! -x "${APP}/Contents/MacOS/$(basename "${APP}" .app)" ]]; then
    die "${APP} has no executable - the Standalone target did not link"
  fi
  if [[ -n "${APP}" ]]; then
    echo "Launching ${APP}"
    open "${APP}"
  else
    echo "No standalone application for this product - effects ship as AU and VST3 only."
  fi
fi

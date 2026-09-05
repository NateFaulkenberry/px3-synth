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
#   scripts/build-product.sh fx --run                every effect, then Logic
#   scripts/build-product.sh --list                  what there is to build
#
# `fx` (or `fx-standalone`) is every product except the Synth, built in one go.
# Working on the effects usually means all of them, and building seven one at a
# time reconfigures seven build directories to compile the same shared code.
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
    printf "  %-14s (target %s)\n" "$(echo "${short}" | tr '[:upper:]' '[:lower:]')" "${target}"
  done
  echo
  echo "Groups:"
  printf "  %-14s every product except the Synth\n" "fx"
  printf "  %-14s the same thing, spelled out\n" "fx-standalone"
}

# The effects: every product that is not the Synth. Derived rather than listed,
# for the same reason TARGETS is - a product added to CMakeLists.txt joins the
# group by existing.
effect_targets() {
  for target in "${TARGETS[@]}"; do
    [[ "${target}" == "PX3Synth" ]] && continue
    echo "${target}"
  done
}

[[ $# -ge 1 ]] || { list_products; exit 0; }

if [[ "$1" == "--list" || "$1" == "-l" ]]; then list_products; exit 0; fi

WANTED="$(echo "$1" | tr '[:upper:]' '[:lower:]')"; shift

# A GROUP builds several products from one configured tree. One build
# directory, so the shared code they all compile is compiled once and cached
# once, rather than seven times in seven trees.
GROUP=false
declare -a GROUP_TARGETS=()

if [[ "${WANTED}" == "fx" || "${WANTED}" == "fx-standalone" ]]; then
  GROUP=true
  while IFS= read -r line; do
    GROUP_TARGETS+=("${line}")
  done < <(effect_targets)
  [[ ${#GROUP_TARGETS[@]} -gt 0 ]] || die "no effect products found in CMakeLists.txt"
fi

TARGET=""
for candidate in "${TARGETS[@]}"; do
  if [[ "$(echo "${candidate#PX3}" | tr '[:upper:]' '[:lower:]')" == "${WANTED}" ]]; then
    TARGET="${candidate}"
    break
  fi
done

if [[ -z "${TARGET}" && "${GROUP}" == false ]]; then
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
if [[ "${GROUP}" == true ]]; then
  BUILD_DIR="build/product-fx"
else
  BUILD_DIR="build/product-$(echo "${TARGET}" | tr '[:upper:]' '[:lower:]')"
fi

if [[ "${GROUP}" == true ]]; then
  echo "==> ${#GROUP_TARGETS[@]} effects: $(echo "${GROUP_TARGETS[@]#PX3}" | tr ' ' ' ')"
  echo "    (${BUILD_TYPE}, debug panel=${DEBUG_PANEL}, install=${INSTALL})"
else
  echo "==> ${TARGET}  (${BUILD_TYPE}, debug panel=${DEBUG_PANEL}, install=${INSTALL})"
fi

# Ninja, like everything else in this project - CI, the benchmarks, the
# diagnostic harnesses and BUILDING.md. Without -G, CMake picked Unix Makefiles
# on macOS, so a developer's per-product build used a generator nothing else
# did, and `--parallel` below became a bare `make -j`, which means no limit at
# all rather than one job per core.
#
# CMake refuses to change generator inside an existing build directory, so a
# tree configured before this change has to be re-made rather than reconfigured.
# Detected and handled here: the alternative is a confusing hard error on
# everyone's first build after pulling.
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  EXISTING_GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" | head -n1)"
  if [[ -n "${EXISTING_GENERATOR}" && "${EXISTING_GENERATOR}" != "Ninja" ]]; then
    echo "    build directory was configured with ${EXISTING_GENERATOR}; reconfiguring with Ninja"
    rm -rf "${BUILD_DIR}"
  fi
fi

cmake -S . -B "${BUILD_DIR}" -G Ninja \
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
# A group takes the formats every one of its products declares. --vst3 and the
# rest still work: they are applied to each product in turn rather than to a
# single target.
if [[ "${GROUP}" == true ]]; then
  AVAILABLE="$(cmake --build "${BUILD_DIR}" --target help 2>/dev/null || true)"
  TARGET_NAMES="$(sed -E 's/^\.\.\. //; s/:.*$//' <<< "${AVAILABLE}")"

  declare -a REQUESTED_FORMATS=("${FORMATS[@]+"${FORMATS[@]}"}")
  FORMATS=()

  for groupTarget in "${GROUP_TARGETS[@]}"; do
    if [[ ${#REQUESTED_FORMATS[@]} -gt 0 ]]; then
      # The flags were parsed against ${TARGET}, which a group does not have.
      # Take the suffix each asked for and apply it to this product.
      for requested in "${REQUESTED_FORMATS[@]}"; do
        suffix="${requested##*_}"
        grep -qx "${groupTarget}_${suffix}" <<< "${TARGET_NAMES}" \
          && FORMATS+=("${groupTarget}_${suffix}")
      done
    elif grep -qx "${groupTarget}_All" <<< "${TARGET_NAMES}"; then
      FORMATS+=("${groupTarget}_All")
    else
      for f in VST3 AU Standalone; do
        grep -qx "${groupTarget}_${f}" <<< "${TARGET_NAMES}" && FORMATS+=("${groupTarget}_${f}")
      done
    fi
  done

  [[ ${#FORMATS[@]} -gt 0 ]] || die "none of the effect products declare a buildable format"
fi

if [[ ${#FORMATS[@]} -eq 0 ]]; then
  # Captured, not piped into grep -q: under `set -o pipefail`, grep -q closes
  # the pipe on its first match, cmake dies of SIGPIPE, and the pipeline
  # reports failure precisely when the target WAS found.
  AVAILABLE="$(cmake --build "${BUILD_DIR}" --target help 2>/dev/null || true)"

  # The two generators list targets differently, and this used to assume one:
  #
  #   Makefiles   ... PX3Chorus_All
  #   Ninja       PX3Chorus_All: phony
  #
  # Matching only the first shape meant that under Ninja nothing matched, the
  # per-format loop found nothing either, and the script died claiming the
  # product declares no buildable format. Normalised to bare names so it reads
  # either - including an old Makefiles tree, which keeps this honest if the
  # reconfigure above is ever removed.
  TARGET_NAMES="$(sed -E 's/^\.\.\. //; s/:.*$//' <<< "${AVAILABLE}")"

  if grep -qx "${TARGET}_All" <<< "${TARGET_NAMES}"; then
    FORMATS=("${TARGET}_All")
  else
    # A single-format product has no _All target; ask for the format directly.
    for f in VST3 AU Standalone; do
      if grep -qx "${TARGET}_${f}" <<< "${TARGET_NAMES}"; then
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

# A group's artefacts live under each product's own directory, so the report
# walks the targets that were built rather than one fixed path.
if [[ "${GROUP}" == true ]]; then
  # Only the formats this run actually built.
  #
  # Walking the artefacts directory instead lists AU bundles after a --vst3
  # run: CMake creates the skeleton for every format it knows about, so the
  # directory exists with nothing in it, and the report credits a build that
  # never happened. Driven off FORMATS, which is what was passed to cmake.
  for built in "${FORMATS[@]}"; do
    groupTarget="${built%_*}"
    suffix="${built##*_}"
    groupArtefacts="${BUILD_DIR}/${groupTarget}_artefacts/${BUILD_TYPE}"

    case "${suffix}" in
      All) subdirs=(VST3 AU Standalone) ;;
      *)   subdirs=("${suffix}") ;;
    esac

    for sub in "${subdirs[@]}"; do
      [[ -d "${groupArtefacts}/${sub}" ]] || continue
      while IFS= read -r bundle; do
        [[ -n "${bundle}" ]] || continue
        # A bundle with no binary in it is a skeleton, not a build.
        binary="${bundle}/Contents/MacOS/$(basename "${bundle}" | sed -E 's/\.(vst3|component|app)$//')"
        [[ -x "${binary}" ]] || continue
        echo "  ${bundle}"
      done < <(find "${groupArtefacts}/${sub}" -maxdepth 1 \
                 \( -name '*.vst3' -o -name '*.component' -o -name '*.app' \))
    done
  done
fi

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
  # An effect has no standalone application - it ships as AU and VST3 - so
  # "run it" means open a host that will load it. The plug-ins are already in
  # ~/Library/Audio/Plug-Ins by this point, which is where the host looks.
  #
  # Only when there is nothing of our own to launch: the Synth still opens its
  # own app, which is faster than a DAW and needs no project.
  if [[ "${GROUP}" == true ]]; then
    HOST="$(ls -d /Applications/Logic\ Pro.app 2>/dev/null | head -n1 || true)"
    if [[ -n "${HOST}" ]]; then
      echo
      echo "Launching ${HOST} - rescan or reopen a project to pick the effects up."
      open "${HOST}"
    else
      echo
      echo "No host found to launch. The effects are installed in"
      echo "~/Library/Audio/Plug-Ins; open your DAW and rescan."
    fi
    exit 0
  fi

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

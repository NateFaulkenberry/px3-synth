#!/usr/bin/env bash
# Builds (if needed) and runs the PX3 CPU benchmark in Release.
#
# Measures per-block processing time across representative scenarios and reports
# it against the real-time budget for the configured block size. Built with
# PX3_DIAGNOSTICS off, so it times the code the plugin actually ships - the
# diagnostic taps in PX3Diag add per-sample work to the very loops being timed.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build/cpubench"
RESULTS_DIR="${REPO_ROOT}/.benchmarks"

FORCE_BUILD=false
BINARY_OVERRIDE=""
SAVE=true
MODE=""

usage() {
  cat <<'EOF'
Usage:
  ./scripts/cpu-benchmark.sh [options]

Options:
  --mode M         Benchmark mode passed through to PX3Bench:
                     (default)     the full scenario table
                     fingerprint   bitwise audio fingerprints
                     ui            editor-related timing
                     selftest      harness self-check
  --build          Force a rebuild before running.
  --binary PATH    Run an already-built PX3Bench instead of building.
  --no-save        Do not write a copy of the output to .benchmarks/
  -h, --help       Show this help.

Examples:
  ./scripts/cpu-benchmark.sh
  ./scripts/cpu-benchmark.sh --build
  ./scripts/cpu-benchmark.sh --mode selftest

Comparing runs:
  Results are timestamped under .benchmarks/, so two runs can be diffed
  directly. Timing is far noisier than memory - close other applications and
  compare medians rather than single figures.
EOF
}

die() { echo "ERROR: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)    MODE="${2:?--mode needs a value}"; shift 2 ;;
    --binary)  BINARY_OVERRIDE="${2:?--binary needs a path}"; shift 2 ;;
    --build)   FORCE_BUILD=true; shift ;;
    --no-save) SAVE=false; shift ;;
    -h|--help) usage; exit 0 ;;
    *)         die "Unknown argument: $1 (try --help)" ;;
  esac
done

BINARY="${BUILD_DIR}/PX3Bench_artefacts/Release/PX3Bench"

if [[ -n "${BINARY_OVERRIDE}" ]]; then
  BINARY="${BINARY_OVERRIDE}"
  [[ -x "${BINARY}" ]] || die "Not an executable: ${BINARY}"
else
  # Release only. A Debug build is several times slower and its numbers
  # describe code nobody ships.
  if [[ "${FORCE_BUILD}" == true || ! -x "${BINARY}" ]]; then
    echo "Building PX3Bench (Release)..."
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DPX3_BUILD_DIAGNOSTIC=ON \
      -DPX3_COPY_PLUGIN_AFTER_BUILD=OFF >/dev/null
    cmake --build "${BUILD_DIR}" --target PX3Bench --parallel >/dev/null
  fi
  [[ -x "${BINARY}" ]] || die "Benchmark binary not produced: ${BINARY}"

  BUILD_TYPE="$(grep -E '^CMAKE_BUILD_TYPE:' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || echo "")"
  if [[ -n "${BUILD_TYPE}" && "${BUILD_TYPE}" != "Release" ]]; then
    die "Build directory is configured as ${BUILD_TYPE}, not Release. Remove ${BUILD_DIR} and retry."
  fi
fi

ARGS=()
[[ -n "${MODE}" ]] && ARGS+=("${MODE}")

if [[ "${SAVE}" == true ]]; then
  mkdir -p "${RESULTS_DIR}"
  RESULT_FILE="${RESULTS_DIR}/cpu-$(date +%Y%m%d-%H%M%S).txt"
  set +e
  "${BINARY}" ${ARGS[@]+"${ARGS[@]}"} | tee "${RESULT_FILE}"
  STATUS=${PIPESTATUS[0]}
  set -e
  echo "Saved: ${RESULT_FILE}"
else
  set +e
  "${BINARY}" ${ARGS[@]+"${ARGS[@]}"}
  STATUS=$?
  set -e
fi

exit ${STATUS}

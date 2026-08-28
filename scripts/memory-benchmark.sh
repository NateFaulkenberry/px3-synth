#!/usr/bin/env bash
# Builds (if needed) and runs the PX3 offline memory benchmark in Release.
#
# The benchmark measures the plugin PROCESSOR's memory in isolation, in a
# process that contains almost nothing else. See docs/MEMORY-BENCHMARK.md for
# what the numbers mean and how they relate to memory in a real DAW.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build/membench"
RESULTS_DIR="${REPO_ROOT}/.benchmarks"
BASELINE_FILE="${RESULTS_DIR}/mem-baseline.json"

INSTANCES=8
SCENARIO="stress"
SAMPLE_RATE=48000
BLOCK_SIZE=512
JSON_OUTPUT=false
RUN_EDITOR=false
SAVE_BASELINE=false
COMPARE_BASELINE=false
TOLERANCE=5
FORCE_BUILD=false
BINARY_OVERRIDE=""

usage() {
  cat <<'EOF'
Usage:
  ./scripts/memory-benchmark.sh [options]

Options:
  --instances N        Highest instance count to reach (default 8).
                       Checkpoints are 1, 2, 4, 8 ... up to N.
  --scenario S         default | initialized | stress   (default stress)
                         default     - construct only, nothing prepared
                         initialized - prepareToPlay plus a few blocks
                         stress      - every FX on, a chord playing, algorithms swept
  --sample-rate HZ     Default 48000.
  --block-size N       Default 512.
  --editor             Also measure editor memory, reported separately.
  --json               Emit JSON instead of the table.
  --save-baseline      Save this run to .benchmarks/mem-baseline.json
  --compare-baseline   Compare this run against that saved baseline.
                       Exits 2 if any checkpoint grew by more than the tolerance.
  --tolerance PCT      Comparison tolerance in per cent (default 5).
  --build              Force a rebuild before running.
  --binary PATH        Run an already-built PX3MemBench instead of building.
  -h, --help           Show this help.

Examples:
  ./scripts/memory-benchmark.sh
  ./scripts/memory-benchmark.sh --instances 16
  ./scripts/memory-benchmark.sh --json
  ./scripts/memory-benchmark.sh --save-baseline
  ./scripts/memory-benchmark.sh --compare-baseline
EOF
}

die() { echo "ERROR: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --instances)        INSTANCES="${2:?--instances needs a value}"; shift 2 ;;
    --scenario)         SCENARIO="${2:?--scenario needs a value}"; shift 2 ;;
    --sample-rate)      SAMPLE_RATE="${2:?--sample-rate needs a value}"; shift 2 ;;
    --block-size)       BLOCK_SIZE="${2:?--block-size needs a value}"; shift 2 ;;
    --tolerance)        TOLERANCE="${2:?--tolerance needs a value}"; shift 2 ;;
    --binary)           BINARY_OVERRIDE="${2:?--binary needs a path}"; shift 2 ;;
    --json)             JSON_OUTPUT=true; shift ;;
    --editor)           RUN_EDITOR=true; shift ;;
    --save-baseline)    SAVE_BASELINE=true; shift ;;
    --compare-baseline) COMPARE_BASELINE=true; shift ;;
    --build)            FORCE_BUILD=true; shift ;;
    -h|--help)          usage; exit 0 ;;
    *)                  die "Unknown argument: $1 (try --help)" ;;
  esac
done

case "${SCENARIO}" in
  default|initialized|stress) ;;
  *) die "Unknown scenario: ${SCENARIO} (expected default, initialized or stress)" ;;
esac

BINARY="${BUILD_DIR}/PX3MemBench_artefacts/Release/PX3MemBench"

if [[ -n "${BINARY_OVERRIDE}" ]]; then
  BINARY="${BINARY_OVERRIDE}"
  [[ -x "${BINARY}" ]] || die "Not an executable: ${BINARY}"
else
  # The benchmark is only meaningful against Release: a Debug build carries
  # different allocator behaviour and unoptimised container layouts, so its
  # numbers describe code nobody ships.
  if [[ "${FORCE_BUILD}" == true || ! -x "${BINARY}" ]]; then
    if [[ "${JSON_OUTPUT}" == false ]]; then
      echo "Building PX3MemBench (Release)..."
    fi
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DPX3_BUILD_DIAGNOSTIC=ON \
      -DPX3_COPY_PLUGIN_AFTER_BUILD=OFF >/dev/null
    cmake --build "${BUILD_DIR}" --target PX3MemBench --parallel >/dev/null
  fi
  [[ -x "${BINARY}" ]] || die "Benchmark binary not produced: ${BINARY}"
fi

# Guard against benchmarking a Debug build by accident.
BUILD_TYPE="$(grep -E '^CMAKE_BUILD_TYPE:' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || echo "")"
if [[ -z "${BINARY_OVERRIDE}" && -n "${BUILD_TYPE}" && "${BUILD_TYPE}" != "Release" ]]; then
  die "Build directory is configured as ${BUILD_TYPE}, not Release. Remove ${BUILD_DIR} and retry."
fi

mkdir -p "${RESULTS_DIR}"

ARGS=(--instances "${INSTANCES}" --scenario "${SCENARIO}"
      --sample-rate "${SAMPLE_RATE}" --block-size "${BLOCK_SIZE}"
      --tolerance "${TOLERANCE}")
[[ "${JSON_OUTPUT}" == true ]] && ARGS+=(--json)
[[ "${RUN_EDITOR}" == true ]] && ARGS+=(--editor)
[[ "${SAVE_BASELINE}" == true ]] && ARGS+=(--save-baseline "${BASELINE_FILE}")
[[ "${COMPARE_BASELINE}" == true ]] && ARGS+=(--compare-baseline "${BASELINE_FILE}")

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
RESULT_FILE="${RESULTS_DIR}/mem-${TIMESTAMP}.json"

# Always keep a machine-readable copy of the run, whichever format was asked
# for on stdout, so results can be compared later without re-running.
"${BINARY}" "${ARGS[@]}" --json > "${RESULT_FILE}" 2>/dev/null || true

set +e
"${BINARY}" "${ARGS[@]}"
STATUS=$?
set -e

if [[ "${JSON_OUTPUT}" == false ]]; then
  echo "Saved: ${RESULT_FILE}"
fi

exit ${STATUS}

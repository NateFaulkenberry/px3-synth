#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build/release"
DIST_ROOT="${REPO_ROOT}/dist"
CMAKE_FILE="${REPO_ROOT}/CMakeLists.txt"

SIGN_MODE=false
SIGN_IDENTITY="${CODESIGN_IDENTITY:-}"
DEBUG_PANEL=false

usage() {
  cat <<'EOF'
Usage:
  ./scripts/build-release.sh [--sign] [--sign-identity "Developer ID Application: ..."] [--debug true|false]

Options:
  --sign                 Sign release artifacts using codesign.
  --sign-identity ID     Explicit signing identity. Overrides CODESIGN_IDENTITY.
  --debug BOOL           Enable in-plugin debug panel UI when BOOL is true. Default: false.
  -h, --help             Show this help.

Environment:
  CODESIGN_IDENTITY      Optional signing identity used with --sign.
EOF
}

die() {
  echo "ERROR: $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

trim() {
  local s="$1"
  s="${s#${s%%[![:space:]]*}}"
  s="${s%${s##*[![:space:]]}}"
  printf '%s' "$s"
}

extract_cmake_value() {
  local key="$1"
  local value
  value="$(grep -E "^[[:space:]]*${key}[[:space:]]+\"?.+\"?$" "${CMAKE_FILE}" | head -n1 || true)"
  [[ -n "$value" ]] || return 1
  value="${value#*${key}}"
  value="$(trim "$value")"
  value="${value%\"}"
  value="${value#\"}"
  printf '%s' "$value"
}

cache_value() {
  local key="$1"
  local file="$2"
  local line
  line="$(grep -E "^${key}:" "$file" | head -n1 || true)"
  [[ -n "$line" ]] || return 1
  printf '%s' "${line#*=}"
}

plist_read() {
  local plist="$1"
  local key="$2"
  /usr/libexec/PlistBuddy -c "Print :${key}" "$plist" 2>/dev/null || true
}

find_bundle() {
  local ext="$1"
  local found
  found="$(find "${BUILD_DIR}" -type d -name "*.${ext}" | sort | head -n1 || true)"
  [[ -n "$found" ]] || return 1
  printf '%s' "$found"
}

arch_check() {
  local binary="$1"
  local label="$2"
  local lipo_out
  lipo_out="$(lipo -info "$binary" 2>&1)"

  if [[ "$lipo_out" != *"arm64"* ]]; then
    die "${label} does not contain arm64: ${lipo_out}"
  fi

  if [[ "$lipo_out" == *"x86_64"* ]]; then
    die "${label} unexpectedly contains x86_64: ${lipo_out}"
  fi

  local file_out
  file_out="$(file "$binary")"
  [[ "$file_out" == *"Mach-O"* ]] || die "${label} is not a Mach-O binary: ${file_out}"
}

verify_bundle() {
  local bundle_path="$1"
  local bundle_kind="$2"

  [[ -d "$bundle_path" ]] || die "${bundle_kind} bundle missing: $bundle_path"

  local plist_path="${bundle_path}/Contents/Info.plist"
  [[ -f "$plist_path" ]] || die "${bundle_kind} Info.plist missing: $plist_path"

  local bundle_id
  bundle_id="$(plist_read "$plist_path" "CFBundleIdentifier")"
  [[ -n "$bundle_id" ]] || die "${bundle_kind} CFBundleIdentifier missing"

  local short_ver
  short_ver="$(plist_read "$plist_path" "CFBundleShortVersionString")"
  [[ -n "$short_ver" ]] || die "${bundle_kind} CFBundleShortVersionString missing"

  local bundle_ver
  bundle_ver="$(plist_read "$plist_path" "CFBundleVersion")"
  [[ -n "$bundle_ver" ]] || die "${bundle_kind} CFBundleVersion missing"

  local executable
  executable="$(plist_read "$plist_path" "CFBundleExecutable")"
  [[ -n "$executable" ]] || die "${bundle_kind} CFBundleExecutable missing"

  local bin_path="${bundle_path}/Contents/MacOS/${executable}"
  [[ -f "$bin_path" ]] || die "${bundle_kind} executable missing: $bin_path"

  arch_check "$bin_path" "$bundle_kind executable"

  echo "  ${bundle_kind} OK"
  echo "    Bundle ID: ${bundle_id}"
  echo "    Version:   ${short_ver} (${bundle_ver})"
  echo "    Binary:    ${bin_path}"
}

maybe_autodetect_sign_identity() {
  if [[ -n "${SIGN_IDENTITY}" ]]; then
    return 0
  fi

  SIGN_IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | grep "Developer ID Application:" | head -n1 | sed -E 's/.*\"(.*)\"/\1/' || true)"
}

sign_and_verify() {
  local artifact="$1"
  local label="$2"

  codesign --force --deep --options runtime --timestamp --sign "${SIGN_IDENTITY}" "$artifact"
  codesign --verify --deep --strict --verbose=2 "$artifact" >/dev/null
  echo "  Signed ${label}: ${artifact}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --sign)
      SIGN_MODE=true
      shift
      ;;
    --sign-identity)
      [[ $# -ge 2 ]] || die "--sign-identity requires a value"
      SIGN_IDENTITY="$2"
      shift 2
      ;;
    --debug)
      [[ $# -ge 2 ]] || die "--debug requires true or false"
      case "$2" in
        [Tt][Rr][Uu][Ee]|1|[Oo][Nn]|[Yy][Ee][Ss])
          DEBUG_PANEL=true
          ;;
        [Ff][Aa][Ll][Ss][Ee]|0|[Oo][Ff][Ff]|[Nn][Oo])
          DEBUG_PANEL=false
          ;;
        *)
          die "Invalid --debug value: $2 (expected true|false)"
          ;;
      esac
      shift 2
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

[[ -f "${CMAKE_FILE}" ]] || die "CMakeLists.txt not found at repository root"

PROJECT_VERSION="$(grep -E '^project\(' "${CMAKE_FILE}" | sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/' | head -n1 || true)"
[[ -n "${PROJECT_VERSION}" ]] || die "Could not determine project version from CMakeLists.txt"

PRODUCT_NAME="$(extract_cmake_value "PRODUCT_NAME" || true)"
[[ -n "${PRODUCT_NAME}" ]] || PRODUCT_NAME="PX3 Synth"

BUNDLE_ID="$(extract_cmake_value "BUNDLE_ID" || true)"
[[ -n "${BUNDLE_ID}" ]] || BUNDLE_ID="(unknown)"

FORMAT_LINE="$(grep -E '^[[:space:]]*FORMATS[[:space:]]+' "${CMAKE_FILE}" | head -n1 || true)"
HAS_STANDALONE=false
if [[ "${FORMAT_LINE}" == *"Standalone"* ]]; then
  HAS_STANDALONE=true
fi

echo "=========================================="
echo "   P(X3) RELEASE BUILD"
echo "=========================================="
echo
echo "P(X3) Release Build"
echo "Version: ${PROJECT_VERSION}"
echo "Platform: macOS"
echo "Architecture: Apple Silicon (arm64)"
echo "Configuration: Release"
echo "Debug Panel: ${DEBUG_PANEL}"
echo "Bundle ID: ${BUNDLE_ID}"
echo

echo "[1/6] Checking environment"
require_cmd cmake
require_cmd xcodebuild
require_cmd file
require_cmd lipo
require_cmd zip
require_cmd grep
require_cmd find
require_cmd sed
require_cmd /usr/libexec/PlistBuddy
if [[ "${SIGN_MODE}" == true ]]; then
  require_cmd codesign
  require_cmd security
fi

echo "[2/6] Configuring CMake"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DPX3_COPY_PLUGIN_AFTER_BUILD=OFF \
  -DPX3_DEBUG_PANEL=$( [[ "${DEBUG_PANEL}" == true ]] && echo ON || echo OFF )

CACHE_FILE="${BUILD_DIR}/CMakeCache.txt"
[[ -f "${CACHE_FILE}" ]] || die "CMake cache not generated"

CONFIGURED_VERSION="$(cache_value "CMAKE_PROJECT_VERSION" "${CACHE_FILE}" || true)"
[[ -n "${CONFIGURED_VERSION}" ]] || die "Could not read CMAKE_PROJECT_VERSION from cache"
PROJECT_VERSION="${CONFIGURED_VERSION}"

DEPLOYMENT_TARGET="$(cache_value "CMAKE_OSX_DEPLOYMENT_TARGET" "${CACHE_FILE}" || true)"
ARCH_VALUE="$(cache_value "CMAKE_OSX_ARCHITECTURES" "${CACHE_FILE}" || true)"
[[ "${ARCH_VALUE}" == "arm64" ]] || die "Configured architecture is not arm64 (found: ${ARCH_VALUE:-<empty>})"

echo "  CMAKE_OSX_ARCHITECTURES=${ARCH_VALUE}"
if [[ -n "${DEPLOYMENT_TARGET}" ]]; then
  echo "  CMAKE_OSX_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
else
  echo "  CMAKE_OSX_DEPLOYMENT_TARGET=(not explicitly set; toolchain default)"
fi

echo "[3/6] Building Release"
cmake --build "${BUILD_DIR}" --config Release --parallel

echo "[4/6] Locating plugins"
AU_BUNDLE="$(find_bundle component || true)"
VST3_BUNDLE="$(find_bundle vst3 || true)"
[[ -n "${AU_BUNDLE}" ]] || die "AU plugin was not produced."
[[ -n "${VST3_BUNDLE}" ]] || die "VST3 plugin was not produced."

APP_BUNDLE=""
if [[ "${HAS_STANDALONE}" == true ]]; then
  APP_BUNDLE="$(find_bundle app || true)"
  [[ -n "${APP_BUNDLE}" ]] || die "Standalone app was expected but not produced."
fi

echo "  AU:   ${AU_BUNDLE}"
echo "  VST3: ${VST3_BUNDLE}"
if [[ -n "${APP_BUNDLE}" ]]; then
  echo "  APP:  ${APP_BUNDLE}"
fi

echo "[5/6] Validating bundles and architecture"
verify_bundle "${AU_BUNDLE}" "AU"
verify_bundle "${VST3_BUNDLE}" "VST3"
if [[ -n "${APP_BUNDLE}" ]]; then
  verify_bundle "${APP_BUNDLE}" "Standalone"
fi

SIGNED_STATE="unsigned"
if [[ "${SIGN_MODE}" == true ]]; then
  echo "  Signing requested"
  maybe_autodetect_sign_identity
  [[ -n "${SIGN_IDENTITY}" ]] || die "--sign was provided but no signing identity was found. Set CODESIGN_IDENTITY or pass --sign-identity."
  echo "  Signing identity: ${SIGN_IDENTITY}"

  sign_and_verify "${AU_BUNDLE}" "AU"
  sign_and_verify "${VST3_BUNDLE}" "VST3"
  if [[ -n "${APP_BUNDLE}" ]]; then
    sign_and_verify "${APP_BUNDLE}" "Standalone"
  fi

  SIGNED_STATE="signed"
else
  echo "  Signing skipped (unsigned release)."
  echo "  Tip: use --sign with CODESIGN_IDENTITY or --sign-identity."
fi

echo "[6/6] Creating distribution"
DIST_DIR="${DIST_ROOT}/PX3-${PROJECT_VERSION}-macOS"
ZIP_PATH="${DIST_ROOT}/P(X3)-${PROJECT_VERSION}-macOS-arm64.zip"

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}/AU" "${DIST_DIR}/VST3"
cp -R "${AU_BUNDLE}" "${DIST_DIR}/AU/"
cp -R "${VST3_BUNDLE}" "${DIST_DIR}/VST3/"

if [[ -n "${APP_BUNDLE}" ]]; then
  mkdir -p "${DIST_DIR}/Standalone"
  cp -R "${APP_BUNDLE}" "${DIST_DIR}/Standalone/"
fi

rm -f "${ZIP_PATH}"
(
  cd "${DIST_ROOT}"
  zip -qry "$(basename "${ZIP_PATH}")" "$(basename "${DIST_DIR}")"
)

echo

echo "=========================================="
echo "   BUILD SUCCESSFUL"
echo "=========================================="
echo
echo "Version: ${PROJECT_VERSION}"
echo "Architecture: arm64"
echo "Signing: ${SIGNED_STATE}"
echo
echo "AU:"
echo "  ${DIST_DIR}/AU/$(basename "${AU_BUNDLE}")"
echo ""
echo "VST3:"
echo "  ${DIST_DIR}/VST3/$(basename "${VST3_BUNDLE}")"
if [[ -n "${APP_BUNDLE}" ]]; then
  echo ""
  echo "Standalone:"
  echo "  ${DIST_DIR}/Standalone/$(basename "${APP_BUNDLE}")"
fi

echo ""
echo "ZIP:"
echo "  ${ZIP_PATH}"

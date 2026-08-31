#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build/release"
DIST_ROOT="${REPO_ROOT}/dist"
CMAKE_FILE="${REPO_ROOT}/CMakeLists.txt"

SIGN_MODE=false
SIGN_IDENTITY="${CODESIGN_IDENTITY:-}"
INSTALLER_SIGN_IDENTITY="${DEVELOPER_ID_INSTALLER:-}"

# Notarisation. Signing proves who built it; notarisation is Apple confirming
# they scanned it, and without it Gatekeeper refuses the download on any machine
# but this one. Stapling attaches the ticket to the artifact so the check works
# with no network.
NOTARIZE_MODE=false
NOTARY_PROFILE="${NOTARY_PROFILE:-}"
NOTARY_AUTH_ARGS=()
DEBUG_PANEL=false
BUILD_UNINSTALLER=true

usage() {
  cat <<'EOF'
Usage:
  ./scripts/build-release.sh [--sign] [--sign-identity "Developer ID Application: ..."] [--debug true|false] [--no-uninstaller]

Options:
  --sign                 Sign release artifacts using codesign.
  --notarize             Sign, then notarise and staple the installer, the
                         uninstaller and the standalone app. Implies --sign.
  --notary-profile NAME  notarytool keychain profile to authenticate with.
                         Implies --notarize.
  --sign-identity ID     Explicit signing identity. Overrides CODESIGN_IDENTITY.
  --debug BOOL           Enable in-plugin debug panel UI when BOOL is true. Default: false.
  --no-uninstaller       Skip building the uninstaller package.
  -h, --help             Show this help.

Environment:
  CODESIGN_IDENTITY      Optional signing identity used with --sign.
  DEVELOPER_ID_INSTALLER Optional installer signing identity for productbuild.
  NOTARY_PROFILE         notarytool keychain profile name.
  NOTARY_KEY / NOTARY_KEY_ID / NOTARY_ISSUER
                         App Store Connect API key, as an alternative.
  APPLE_ID / APPLE_APP_PASSWORD / APPLE_TEAM_ID
                         Apple ID credentials, as a further alternative.
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

extract_px3_version() {
  local line
  line="$(grep -E '^[[:space:]]*set\([[:space:]]*PX3_VERSION[[:space:]]+"?[0-9]+\.[0-9]+\.[0-9]+"?[[:space:]]*\)' "${CMAKE_FILE}" | head -n1 || true)"
  [[ -n "$line" ]] || return 1

  line="${line#*PX3_VERSION}"
  line="$(trim "$line")"
  line="${line%)}"
  line="$(trim "$line")"
  line="${line%\"}"
  line="${line#\"}"
  printf '%s' "$line"
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

pkg_payload_has() {
  local pkg_path="$1"
  local needle="$2"
  pkgutil --payload-files "$pkg_path" | grep -F "$needle" >/dev/null 2>&1
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

# productbuild only copies the resources its Distribution actually references -
# welcome, licence, background and so on. A helper executable that only the
# installation-check JavaScript calls is silently dropped, and the check then
# fails to find it. Rather than let the check quietly no-op, the file is put
# back by expanding the product, adding it, and re-flattening.
#
# Flattening discards any signature, so signing moves to a productsign pass
# afterwards rather than being done by productbuild.
inject_package_resources() {
  local pkg="$1"
  local source_dir="$2"
  shift 2
  local work="${PKG_WORK_DIR}/inject-$(basename "${pkg%.pkg}")"

  rm -rf "${work}"
  pkgutil --expand "${pkg}" "${work}" >/dev/null 2>&1 \
    || die "Could not expand package to inject resources: ${pkg}"

  mkdir -p "${work}/Resources"
  local file
  for file in "$@"; do
    cp "${source_dir}/${file}" "${work}/Resources/"
    case "${file}" in
      *.sh) chmod +x "${work}/Resources/${file}" ;;
    esac
  done

  rm -f "${pkg}"
  pkgutil --flatten "${work}" "${pkg}" >/dev/null 2>&1 \
    || die "Could not re-flatten package after injecting resources: ${pkg}"
}

sign_product_if_requested() {
  local pkg="$1"
  [[ -n "${INSTALLER_SIGN_IDENTITY}" ]] || return 0
  local signed="${pkg%.pkg}-signed.pkg"
  productsign --sign "${INSTALLER_SIGN_IDENTITY}" "${pkg}" "${signed}" \
    || die "productsign failed for ${pkg}"
  mv -f "${signed}" "${pkg}"
}

# Running-host detection.
#
# Identity comes from BUNDLE IDENTIFIERS matched against
# scripts/installer/au-hosts.tsv, which is the single source of truth and the
# only place identifiers are written. The detector and the database are copied
# into both packages verbatim, so the installer, the uninstaller and the tests
# all run exactly the same code.
#
# This replaced a process-name search. Names like "Live", "Reason" and "LUNA"
# are ordinary words, `ps -A` also lists daemons and other users' processes, and
# the old list included `auval` - which macOS runs by itself after any Audio
# Unit is installed, so the installer blocked itself moments after succeeding.
AU_HOST_DETECTOR="${REPO_ROOT}/scripts/installer/detect-au-hosts.sh"
AU_HOST_DATABASE="${REPO_ROOT}/scripts/installer/au-hosts.tsv"

install_host_detection() {
  local dest_dir="$1"
  [[ -f "${AU_HOST_DETECTOR}" ]] || die "Missing host detector: ${AU_HOST_DETECTOR}"
  [[ -f "${AU_HOST_DATABASE}" ]] || die "Missing host database: ${AU_HOST_DATABASE}"
  cp "${AU_HOST_DETECTOR}" "${dest_dir}/detect-au-hosts.sh"
  cp "${AU_HOST_DATABASE}" "${dest_dir}/au-hosts.tsv"
  chmod +x "${dest_dir}/detect-au-hosts.sh"
}

# The preinstall script is the ONLY gate, deliberately.
#
# The previous design also ran an installation-check in the Distribution, which
# called system.run and then read the host names back from a plist in /tmp.
# That was wrong twice over: system.run is unreliable in the modern Installer's
# JavaScript context, and when it failed the JavaScript fell through to reading
# that file - which is world-writable, unauthenticated, and has no freshness
# guarantee. A stale copy made the installer block and name applications that
# were not running, and in one case were not even installed.
#
# There is now no shared mutable state between the check and what the user is
# shown. The preinstall script runs as root immediately before the payload is
# written, which is both the most reliable place to run it and the latest
# possible moment - so a DAW opened while the installer sat waiting is caught.
write_host_preinstall() {
  cat > "$1" <<'PREEOF'
#!/bin/sh
# Refuses to proceed while a known Audio Unit host is running. A host with the
# plug-in loaded holds the bundle open, so replacing or removing it underneath
# leaves that host running stale code.
set -u
SELF_DIR=$(CD=$(dirname "$0"); cd "$CD" && pwd)

HOSTS=$("${SELF_DIR}/detect-au-hosts.sh" 2>/dev/null)
STATUS=$?

if [ ${STATUS} -eq 0 ] || [ -z "${HOSTS}" ]; then
    exit 0
fi

LIST=$(printf '%s' "${HOSTS}" | sed 's/^/  - /')

# The Installer only shows a generic failure, so the specific message is put in
# front of the user directly. It is shown AS THE CONSOLE USER: this script runs
# as root, and a dialog opened by root never appears in the user's session.
CONSOLE_USER=$(stat -f%Su /dev/console 2>/dev/null)
if [ -n "${CONSOLE_USER}" ] && [ "${CONSOLE_USER}" != "root" ]; then
    CONSOLE_UID=$(id -u "${CONSOLE_USER}" 2>/dev/null)
    if [ -n "${CONSOLE_UID}" ]; then
        MESSAGE="PX3 Synth cannot continue while these applications are running:

${LIST}

Please save your work, close them, and run the installer again."
        # Backgrounded: the Installer is blocked until this script returns, and
        # a modal that outlives it would strand the whole installation.
        launchctl asuser "${CONSOLE_UID}" sudo -u "${CONSOLE_USER}" \
            /usr/bin/osascript -e "display dialog \"${MESSAGE}\" buttons {\"OK\"} default button 1 with icon caution with title \"PX3 Synth Installer\"" \
            >/dev/null 2>&1 &
        sleep 1
    fi
fi

echo "PX3 Synth cannot continue while these applications are running:" >&2
printf '%s\n' "${LIST}" >&2
echo "Close them and run this installer again." >&2
exit 1
PREEOF
  chmod +x "$1"
}

# Puts the branding image the macOS Installer shows in the bottom-left of its
# window into a package's resources directory, and echoes the file name for the
# Distribution XML. Echoes nothing if no usable asset exists, in which case the
# installer simply has no background.
#
# An author-supplied PNG wins over the GIF: the GIF is opaque, and a PNG with an
# alpha channel sits better against the Installer's light pane. Drop one at
# Source/Assets/px3-installer.png and it will be used as-is.
BRANDING_PNG_SOURCE="${REPO_ROOT}/Source/Assets/px3-installer.png"
BRANDING_GIF_SOURCE="${REPO_ROOT}/Source/Assets/px3.gif"
BRANDING_WIDTH_PX=180

prepare_installer_branding() {
  local dest_dir="$1"
  local dest_file="${dest_dir}/px3-brand.png"

  if [[ -f "${BRANDING_PNG_SOURCE}" ]]; then
    cp "${BRANDING_PNG_SOURCE}" "${dest_file}" 2>/dev/null || return 0
    printf '%s' "px3-brand.png"
    return 0
  fi

  if [[ -f "${BRANDING_GIF_SOURCE}" ]] && command -v sips >/dev/null 2>&1; then
    # The source is an animated GIF; the Installer renders a still, so it is
    # converted to PNG rather than handed over as-is, and scaled down from
    # 2.3 MB to something proportionate to the space it occupies.
    local tmp_png="${dest_dir}/.px3-brand-full.png"
    if sips -s format png "${BRANDING_GIF_SOURCE}" --out "${tmp_png}" >/dev/null 2>&1 \
       && sips -Z "${BRANDING_WIDTH_PX}" "${tmp_png}" --out "${dest_file}" >/dev/null 2>&1; then
      rm -f "${tmp_png}"
      printf '%s' "px3-brand.png"
      return 0
    fi
    rm -f "${tmp_png}"
  fi

  return 0
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

# Works out how notarytool should authenticate, once, and fails now rather than
# after a build has run. A keychain profile is preferred because it keeps the
# app-specific password out of the environment and out of this script's output.
resolve_notary_credentials() {
  if [[ -n "${NOTARY_PROFILE}" ]]; then
    NOTARY_AUTH_ARGS=(--keychain-profile "${NOTARY_PROFILE}")
  elif [[ -n "${NOTARY_KEY:-}" && -n "${NOTARY_KEY_ID:-}" && -n "${NOTARY_ISSUER:-}" ]]; then
    NOTARY_AUTH_ARGS=(--key "${NOTARY_KEY}" --key-id "${NOTARY_KEY_ID}" --issuer "${NOTARY_ISSUER}")
  elif [[ -n "${APPLE_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]; then
    NOTARY_AUTH_ARGS=(--apple-id "${APPLE_ID}" --password "${APPLE_APP_PASSWORD}" --team-id "${APPLE_TEAM_ID}")
  else
    die "No notarisation credentials. Set NOTARY_PROFILE to a profile stored with
  xcrun notarytool store-credentials <name> --apple-id <id> --team-id <team> --password <app-specific>
or set NOTARY_KEY/NOTARY_KEY_ID/NOTARY_ISSUER, or APPLE_ID/APPLE_APP_PASSWORD/APPLE_TEAM_ID."
  fi

  # Proves the credentials work before anything is submitted, so a bad password
  # costs a second rather than a full build.
  #
  # No --limit: notarytool 1.1.2 does not have that option, and passing it made
  # this abort every build on that version while blaming the credentials. The
  # output is read rather than just the exit status so a usage error can be
  # told apart from a refusal, and an account with nothing submitted answers
  # "No submission history" - which is an authenticated round-trip, so a pass.
  local notary_out
  notary_out="$(xcrun notarytool history "${NOTARY_AUTH_ARGS[@]}" 2>&1)"
  case "${notary_out}" in
    *"Unknown option"*|*"Usage:"*)
      die "notarytool rejected the command, not the credentials: $(printf '%s' "${notary_out}" | head -n1)" ;;
    *"No submission history"*|*"createdDate"*|*"id: "*)
      : ;;
    *)
      die "Notarisation credentials were rejected by Apple: $(printf '%s' "${notary_out}" | head -n1)" ;;
  esac
}

# Submits one artifact, waits for the verdict, and staples the ticket to it.
#
# A bundle cannot be submitted directly - notarytool takes a zip, a pkg or a
# dmg - so an app is zipped for the submission and the ticket is stapled to the
# ORIGINAL bundle afterwards. Stapling the zip would achieve nothing: the zip is
# thrown away and the bundle is what gets shipped.
notarize_and_staple() {
  local artifact="$1"
  local label="$2"
  local submission="${artifact}"
  local scratch=""

  if [[ -d "${artifact}" ]]; then
    scratch="$(mktemp -d)"
    submission="${scratch}/$(basename "${artifact}").zip"
    ditto -c -k --keepParent "${artifact}" "${submission}" \
      || die "Could not archive ${label} for notarisation"
  fi

  echo "  Notarising ${label} (this waits on Apple and can take several minutes)..."
  if ! xcrun notarytool submit "${submission}" "${NOTARY_AUTH_ARGS[@]}" --wait; then
    [[ -n "${scratch}" ]] && rm -rf "${scratch}"
    die "Notarisation failed for ${label}. Run: xcrun notarytool log <submission-id> ${NOTARY_AUTH_ARGS[*]}"
  fi
  [[ -n "${scratch}" ]] && rm -rf "${scratch}"

  xcrun stapler staple "${artifact}" || die "Could not staple the ticket to ${label}"
  xcrun stapler validate "${artifact}" >/dev/null || die "Stapled ticket does not validate for ${label}"
  echo "  Notarised and stapled ${label}: ${artifact}"
}

# Only meaningful once the artifact is both signed and stapled - this is the
# check the user's machine performs on first launch.
notarize_if_requested() {
  [[ "${NOTARIZE_MODE}" == true ]] || return 0
  notarize_and_staple "$1" "$2"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --sign)
      SIGN_MODE=true
      shift
      ;;
    --notarize)
      # Notarisation is only meaningful on a signed artifact, so it implies it
      # rather than failing later with an unsigned-binary rejection from Apple.
      SIGN_MODE=true
      NOTARIZE_MODE=true
      shift
      ;;
    --notary-profile)
      [[ $# -ge 2 ]] || die "--notary-profile requires a value"
      NOTARY_PROFILE="$2"
      SIGN_MODE=true
      NOTARIZE_MODE=true
      shift 2
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
    --no-uninstaller)
      BUILD_UNINSTALLER=false
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

[[ -f "${CMAKE_FILE}" ]] || die "CMakeLists.txt not found at repository root"

PROJECT_VERSION="$(extract_px3_version || true)"
[[ -n "${PROJECT_VERSION}" ]] || die "Could not determine PX3_VERSION from CMakeLists.txt"
[[ "${PROJECT_VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "PX3_VERSION is not valid SemVer (expected MAJOR.MINOR.PATCH)"

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

echo "[1/8] Checking environment"
require_cmd cmake
require_cmd xcodebuild
require_cmd file
require_cmd lipo
require_cmd zip
require_cmd pkgbuild
require_cmd productbuild
require_cmd productsign
require_cmd pkgutil
require_cmd grep
require_cmd find
require_cmd sed
require_cmd /usr/libexec/PlistBuddy
if [[ "${NOTARIZE_MODE}" == true ]]; then
  require_cmd xcrun
  require_cmd ditto
  resolve_notary_credentials
fi

if [[ "${SIGN_MODE}" == true ]]; then
  require_cmd codesign
  require_cmd security
fi

# The icon is committed, so this only refreshes it when the tooling is present.
# A release must not fail because node or sharp is missing on the build machine.
ICON_SOURCE="${REPO_ROOT}/Source/Assets/px3.gif"
ICON_OUTPUT="${REPO_ROOT}/Source/Assets/px3-icon.png"
if [[ -f "${REPO_ROOT}/scripts/make-app-icon.mjs" && -f "${ICON_SOURCE}" ]] && command -v node >/dev/null 2>&1; then
  if node "${REPO_ROOT}/scripts/make-app-icon.mjs" >/dev/null 2>&1; then
    echo "  App icon regenerated from $(basename "${ICON_SOURCE}")"
  elif [[ -f "${ICON_OUTPUT}" ]]; then
    echo "  App icon: using committed ${ICON_OUTPUT##*/} (regeneration needs sharp: npm install --prefix .tools sharp)"
  else
    echo "  App icon: none (install sharp to generate one: npm install --prefix .tools sharp)"
  fi
elif [[ -f "${ICON_OUTPUT}" ]]; then
  echo "  App icon: using committed ${ICON_OUTPUT##*/}"
fi

# The uninstaller's icon is the application's with a red X struck through it, so
# the two are instantly distinguishable in a folder while still obviously
# belonging to the same product. Generated FROM the app icon above, so it
# follows any change to it rather than being a second asset to remember.
#
# Committed like the app icon, and skipped the same way when the tooling is
# absent: a release must not fail because node or sharp is missing.
UNINSTALL_ICON_OUTPUT="${REPO_ROOT}/Source/Assets/px3-uninstall.icns"
if [[ -f "${REPO_ROOT}/scripts/make-uninstaller-icon.mjs" && -f "${ICON_OUTPUT}" ]] \
   && command -v node >/dev/null 2>&1; then
  if node "${REPO_ROOT}/scripts/make-uninstaller-icon.mjs" >/dev/null 2>&1; then
    echo "  Uninstaller icon regenerated from $(basename "${ICON_OUTPUT}")"
  elif [[ -f "${UNINSTALL_ICON_OUTPUT}" ]]; then
    echo "  Uninstaller icon: using committed ${UNINSTALL_ICON_OUTPUT##*/}"
  else
    echo "  Uninstaller icon: none (install sharp to generate one: npm install --prefix .tools sharp)"
  fi
elif [[ -f "${UNINSTALL_ICON_OUTPUT}" ]]; then
  echo "  Uninstaller icon: using committed ${UNINSTALL_ICON_OUTPUT##*/}"
fi

echo "[2/8] Configuring CMake"
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

echo "[3/8] Building Release"
cmake --build "${BUILD_DIR}" --config Release --parallel

echo "[4/8] Locating plugins"
AU_BUNDLE="$(find_bundle component || true)"
VST3_BUNDLE="$(find_bundle vst3 || true)"
if [[ -z "${AU_BUNDLE}" ]]; then
  die "P(X3) AU plugin was not found under: ${BUILD_DIR}"
fi
if [[ -z "${VST3_BUNDLE}" ]]; then
  die "P(X3) VST3 plugin was not found under: ${BUILD_DIR}"
fi

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

echo "[5/8] Validating bundles and architecture"
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
    # Before the pkg is built from it, so the installed app carries its own
    # ticket and passes Gatekeeper on first launch with no network.
    notarize_if_requested "${APP_BUNDLE}" "Standalone"
  fi

  SIGNED_STATE="signed"
else
  echo "  Signing skipped (unsigned release)."
  echo "  Tip: use --sign with CODESIGN_IDENTITY or --sign-identity."
fi

echo "[6/8] Creating distribution and packaging"
DIST_DIR="${DIST_ROOT}/PX3-v${PROJECT_VERSION}-macOS"
ZIP_PATH="${DIST_ROOT}/P(X3)-v${PROJECT_VERSION}-macOS-arm64.zip"
PKG_PATH="${DIST_ROOT}/PX3-v${PROJECT_VERSION}.pkg"
PKG_WORK_DIR="${BUILD_DIR}/installer"
PKG_COMPONENTS_DIR="${PKG_WORK_DIR}/packages"
PKG_ROOT_AU="${PKG_WORK_DIR}/root-au"
PKG_ROOT_VST3="${PKG_WORK_DIR}/root-vst3"
PKG_ROOT_APP="${PKG_WORK_DIR}/root-app"
PKG_EXPANDED_DIR="${PKG_WORK_DIR}/expanded-product"
PRODUCT_DISTRIBUTION_XML="${PKG_WORK_DIR}/Distribution.xml"

AU_NAME="$(basename "${AU_BUNDLE}")"
VST3_NAME="$(basename "${VST3_BUNDLE}")"

AU_COMPONENT_PKG="${PKG_COMPONENTS_DIR}/PX3-AU-v${PROJECT_VERSION}.pkg"
VST3_COMPONENT_PKG="${PKG_COMPONENTS_DIR}/PX3-VST3-v${PROJECT_VERSION}.pkg"
APP_COMPONENT_PKG="${PKG_COMPONENTS_DIR}/PX3-Standalone-v${PROJECT_VERSION}.pkg"
UI_CONFIG_RESOURCE_REL="Contents/Resources/UIConfig.json"

AU_INSTALL_DIR="Library/Audio/Plug-Ins/Components"
VST3_INSTALL_DIR="Library/Audio/Plug-Ins/VST3"
APP_INSTALL_DIR="Applications"

PACKAGE_ID_BASE="${BUNDLE_ID}"
if [[ "${PACKAGE_ID_BASE}" == "(unknown)" || -z "${PACKAGE_ID_BASE}" ]]; then
  PACKAGE_ID_BASE="com.px3.px3synth"
fi
AU_PACKAGE_ID="${PACKAGE_ID_BASE}.au"
VST3_PACKAGE_ID="${PACKAGE_ID_BASE}.vst3"
APP_PACKAGE_ID="${PACKAGE_ID_BASE}.standalone"

rm -rf "${PKG_WORK_DIR}"
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}/AU" "${DIST_DIR}/VST3"
mkdir -p "${PKG_COMPONENTS_DIR}"
mkdir -p "${PKG_ROOT_AU}/${AU_INSTALL_DIR}"
mkdir -p "${PKG_ROOT_VST3}/${VST3_INSTALL_DIR}"
cp -R "${AU_BUNDLE}" "${DIST_DIR}/AU/"
cp -R "${VST3_BUNDLE}" "${DIST_DIR}/VST3/"
cp -R "${AU_BUNDLE}" "${PKG_ROOT_AU}/${AU_INSTALL_DIR}/"
cp -R "${VST3_BUNDLE}" "${PKG_ROOT_VST3}/${VST3_INSTALL_DIR}/"

[[ -f "${AU_BUNDLE}/${UI_CONFIG_RESOURCE_REL}" ]] \
  || die "AU bundle is missing required UI config resource: ${AU_BUNDLE}/${UI_CONFIG_RESOURCE_REL}"

[[ -f "${VST3_BUNDLE}/${UI_CONFIG_RESOURCE_REL}" ]] \
  || die "VST3 bundle is missing required UI config resource: ${VST3_BUNDLE}/${UI_CONFIG_RESOURCE_REL}"

if [[ -n "${APP_BUNDLE}" ]]; then
  mkdir -p "${DIST_DIR}/Standalone"
  cp -R "${APP_BUNDLE}" "${DIST_DIR}/Standalone/"
  mkdir -p "${PKG_ROOT_APP}/${APP_INSTALL_DIR}"
  cp -R "${APP_BUNDLE}" "${PKG_ROOT_APP}/${APP_INSTALL_DIR}/"
fi

rm -f "${ZIP_PATH}"
rm -f "${PKG_PATH}"

# Every component package carries the same preinstall guard, so a running host
# stops the install even if the Installer's JavaScript check did not run.
PKG_SHARED_SCRIPTS_DIR="${PKG_WORK_DIR}/component-scripts"
mkdir -p "${PKG_SHARED_SCRIPTS_DIR}"
install_host_detection "${PKG_SHARED_SCRIPTS_DIR}"
write_host_preinstall "${PKG_SHARED_SCRIPTS_DIR}/preinstall"

# pkgbuild creates individual component packages.
# Each component package contains one plugin format and encodes the target
# install location for that format.
pkgbuild \
  --root "${PKG_ROOT_AU}" \
  --scripts "${PKG_SHARED_SCRIPTS_DIR}" \
  --identifier "${AU_PACKAGE_ID}" \
  --version "${PROJECT_VERSION}" \
  "${AU_COMPONENT_PKG}"

pkgbuild \
  --root "${PKG_ROOT_VST3}" \
  --scripts "${PKG_SHARED_SCRIPTS_DIR}" \
  --identifier "${VST3_PACKAGE_ID}" \
  --version "${PROJECT_VERSION}" \
  "${VST3_COMPONENT_PKG}"

if [[ -n "${APP_BUNDLE}" ]]; then
  pkgbuild \
    --root "${PKG_ROOT_APP}" \
    --scripts "${PKG_SHARED_SCRIPTS_DIR}" \
    --identifier "${APP_PACKAGE_ID}" \
    --version "${PROJECT_VERSION}" \
    "${APP_COMPONENT_PKG}"
fi

# Build an explicit product distribution so Installer UI labels always use
# branded names instead of tool defaults that can inherit target names.
#
# customize="always" forces the Installation Type pane to appear, so the user
# picks which formats to install rather than getting all of them silently. Each
# format is its own visible, independently selectable choice.
PRODUCT_RESOURCES_DIR="${PKG_WORK_DIR}/resources"
mkdir -p "${PRODUCT_RESOURCES_DIR}"

# The detector lives in the component packages' preinstall scripts, not in the
# product's Resources - there is no JavaScript check to serve any more.
INSTALLER_HOST_CHECK_XML=""

INSTALLER_BRANDING_FILE="$(prepare_installer_branding "${PRODUCT_RESOURCES_DIR}")"
INSTALLER_BACKGROUND_XML=""
if [[ -n "${INSTALLER_BRANDING_FILE}" ]]; then
  # alignment="bottomleft" with no scaling puts the logo in the bottom-left
  # corner of the Installer window at its natural size. The darkAqua variant
  # keeps it present in dark mode, which otherwise falls back to no background.
  INSTALLER_BACKGROUND_XML="  <background file=\"${INSTALLER_BRANDING_FILE}\" mime-type=\"image/png\" alignment=\"bottomleft\" scaling=\"none\"/>
  <background-darkAqua file=\"${INSTALLER_BRANDING_FILE}\" mime-type=\"image/png\" alignment=\"bottomleft\" scaling=\"none\"/>"
  echo "  Installer branding: ${PRODUCT_RESOURCES_DIR}/${INSTALLER_BRANDING_FILE}"
else
  echo "  Installer branding: none found (skipping background)"
fi

cat > "${PRODUCT_RESOURCES_DIR}/welcome.txt" <<EOF
P(X3) v${PROJECT_VERSION}

This installer lets you choose which formats to install.

Click Continue, then use the Customize button on the Installation Type pane to
select or deselect any of:

  - Audio Unit (AU)   -> /Library/Audio/Plug-Ins/Components
  - VST3              -> /Library/Audio/Plug-Ins/VST3
  - Standalone app    -> /Applications

All formats are selected by default. Presets are created automatically the
first time you run P(X3).
EOF

# The standalone choice and its package reference only exist when the
# standalone target was actually built.
APP_CHOICE_OUTLINE=""
APP_CHOICE_DEF=""
APP_PKG_REF=""
if [[ -n "${APP_BUNDLE}" ]]; then
  APP_CHOICE_OUTLINE='      <line choice="px3.standalone"/>'
  APP_CHOICE_DEF="  <choice id=\"px3.standalone\" title=\"Standalone Application\" description=\"The P(X3) standalone app, installed to /Applications. Use this to play P(X3) without a DAW.\" start_selected=\"true\">
    <pkg-ref id=\"${APP_PACKAGE_ID}\"/>
  </choice>"
  APP_PKG_REF="  <pkg-ref id=\"${APP_PACKAGE_ID}\" version=\"${PROJECT_VERSION}\">#$(basename "${APP_COMPONENT_PKG}")</pkg-ref>"
fi

cat > "${PRODUCT_DISTRIBUTION_XML}" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>PX3</title>
  <welcome file="welcome.txt"/>
${INSTALLER_BACKGROUND_XML}
${INSTALLER_HOST_CHECK_XML}
  <options customize="always" require-scripts="false" hostArchitectures="x86_64,arm64"/>
  <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>

  <choices-outline>
    <line choice="px3.au"/>
    <line choice="px3.vst3"/>
${APP_CHOICE_OUTLINE}
  </choices-outline>

  <choice id="px3.au" title="Audio Unit (AU)" description="For Logic Pro, GarageBand, MainStage and other AU hosts. Installs to /Library/Audio/Plug-Ins/Components." start_selected="true">
    <pkg-ref id="${AU_PACKAGE_ID}"/>
  </choice>

  <choice id="px3.vst3" title="VST3" description="For Ableton Live, Cubase, Reaper, Studio One, Bitwig and other VST3 hosts. Installs to /Library/Audio/Plug-Ins/VST3." start_selected="true">
    <pkg-ref id="${VST3_PACKAGE_ID}"/>
  </choice>

${APP_CHOICE_DEF}

  <pkg-ref id="${AU_PACKAGE_ID}" version="${PROJECT_VERSION}">#$(basename "${AU_COMPONENT_PKG}")</pkg-ref>
  <pkg-ref id="${VST3_PACKAGE_ID}" version="${PROJECT_VERSION}">#$(basename "${VST3_COMPONENT_PKG}")</pkg-ref>
${APP_PKG_REF}
</installer-gui-script>
EOF

PRODUCTBUILD_ARGS=(
  --distribution "${PRODUCT_DISTRIBUTION_XML}"
  --package-path "${PKG_COMPONENTS_DIR}"
  --resources "${PRODUCT_RESOURCES_DIR}"
)

PKG_SIGN_STATE="unsigned"
if [[ -n "${INSTALLER_SIGN_IDENTITY}" ]]; then
  PKG_SIGN_STATE="signed"
fi
PRODUCTBUILD_ARGS+=("${PKG_PATH}")

productbuild "${PRODUCTBUILD_ARGS[@]}"
sign_product_if_requested "${PKG_PATH}"
notarize_if_requested "${PKG_PATH}" "Installer"

[[ -f "${AU_COMPONENT_PKG}" ]] || die "AU component package not created: ${AU_COMPONENT_PKG}"
[[ -f "${VST3_COMPONENT_PKG}" ]] || die "VST3 component package not created: ${VST3_COMPONENT_PKG}"
if [[ -n "${APP_BUNDLE}" ]]; then
  [[ -f "${APP_COMPONENT_PKG}" ]] || die "Standalone component package not created: ${APP_COMPONENT_PKG}"
fi
[[ -f "${PKG_PATH}" ]] || die "Final installer package not created: ${PKG_PATH}"
[[ -s "${PKG_PATH}" ]] || die "Final installer package is empty: ${PKG_PATH}"

rm -rf "${PKG_EXPANDED_DIR}"
pkgutil --expand "${PKG_PATH}" "${PKG_EXPANDED_DIR}" >/dev/null 2>&1 \
  || die "pkgutil could not inspect installer via expand: ${PKG_PATH}"

[[ -f "${PKG_EXPANDED_DIR}/Distribution" ]] \
  || die "Expanded installer is missing Distribution file: ${PKG_EXPANDED_DIR}/Distribution"

grep -F "$(basename "${AU_COMPONENT_PKG}")" "${PKG_EXPANDED_DIR}/Distribution" >/dev/null 2>&1 \
  || die "Final installer does not reference AU component package"

grep -F "$(basename "${VST3_COMPONENT_PKG}")" "${PKG_EXPANDED_DIR}/Distribution" >/dev/null 2>&1 \
  || die "Final installer does not reference VST3 component package"

# The point of the format selection is that the user gets a choice, so verify
# the choices actually reached the Distribution rather than trusting the XML
# heredoc to have expanded correctly.
grep -F 'customize="always"' "${PKG_EXPANDED_DIR}/Distribution" >/dev/null 2>&1 \
  || die "Final installer does not present the Installation Type selection pane"

# The running-host check is only useful if its helper actually shipped.
# productbuild drops unreferenced resources, so this is verified rather than
# assumed - without the helper the check silently passes and blocks nothing.
# The detector ships inside each component package's Scripts directory, which
# pkgbuild always includes, so there is nothing for productbuild to drop.

for choice_id in "px3.au" "px3.vst3"; do
  grep -F "\"${choice_id}\"" "${PKG_EXPANDED_DIR}/Distribution" >/dev/null 2>&1 \
    || die "Final installer is missing the ${choice_id} install choice"
done

if [[ -n "${APP_BUNDLE}" ]]; then
  grep -F "$(basename "${APP_COMPONENT_PKG}")" "${PKG_EXPANDED_DIR}/Distribution" >/dev/null 2>&1 \
    || die "Final installer does not reference Standalone component package"
  grep -F '"px3.standalone"' "${PKG_EXPANDED_DIR}/Distribution" >/dev/null 2>&1 \
    || die "Final installer is missing the Standalone install choice"
  pkg_payload_has "${APP_COMPONENT_PKG}" "${APP_INSTALL_DIR}/$(basename "${APP_BUNDLE}")/Contents/Info.plist" \
    || die "Standalone component package payload validation failed"
fi

pkg_payload_has "${AU_COMPONENT_PKG}" "${AU_INSTALL_DIR}/${AU_NAME}/Contents/Info.plist" \
  || die "AU component package payload validation failed: missing ${AU_INSTALL_DIR}/${AU_NAME}/Contents/Info.plist"

pkg_payload_has "${AU_COMPONENT_PKG}" "${AU_INSTALL_DIR}/${AU_NAME}/${UI_CONFIG_RESOURCE_REL}" \
  || die "AU component package payload validation failed: missing ${AU_INSTALL_DIR}/${AU_NAME}/${UI_CONFIG_RESOURCE_REL}"

pkg_payload_has "${VST3_COMPONENT_PKG}" "${VST3_INSTALL_DIR}/${VST3_NAME}/Contents/Info.plist" \
  || die "VST3 component package payload validation failed: missing ${VST3_INSTALL_DIR}/${VST3_NAME}/Contents/Info.plist"

pkg_payload_has "${VST3_COMPONENT_PKG}" "${VST3_INSTALL_DIR}/${VST3_NAME}/${UI_CONFIG_RESOURCE_REL}" \
  || die "VST3 component package payload validation failed: missing ${VST3_INSTALL_DIR}/${VST3_NAME}/${UI_CONFIG_RESOURCE_REL}"

if ! pkgutil --check-signature "${PKG_PATH}" >/dev/null 2>&1; then
  echo "  Warning: pkgutil signature check reported unsigned installer (acceptable for local dev)."
fi

echo "[7/8] Building uninstaller application"
# Deliberately an application, not a .pkg.
#
# The macOS Installer always presents install-style UI - a Destination Select
# pane, an Installation Type pane, and an "Install" button - and none of that
# can be relabelled from a Distribution file. An uninstaller shipped that way
# reads as an installer whatever the panes say. Owning the app means owning
# every word the user sees, and it gets the system's own authorisation prompt
# instead of an installer's.
UNINSTALLER_APP_PATH="${DIST_ROOT}/PX3 Uninstaller.app"
UNINSTALLER_SIGN_STATE="not built"

if [[ "${BUILD_UNINSTALLER}" == true ]]; then
  require_cmd osacompile

  UNINSTALLER_SCRIPT="${REPO_ROOT}/scripts/installer/uninstaller.applescript"
  UNINSTALLER_REMOVAL="${REPO_ROOT}/scripts/installer/px3-uninstall.sh"
  [[ -f "${UNINSTALLER_SCRIPT}" ]] || die "Missing uninstaller script: ${UNINSTALLER_SCRIPT}"
  [[ -f "${UNINSTALLER_REMOVAL}" ]] || die "Missing removal script: ${UNINSTALLER_REMOVAL}"

  rm -rf "${UNINSTALLER_APP_PATH}"
  osacompile -o "${UNINSTALLER_APP_PATH}" "${UNINSTALLER_SCRIPT}" \
    || die "osacompile failed for the uninstaller"

  UNINSTALLER_RES="${UNINSTALLER_APP_PATH}/Contents/Resources"
  mkdir -p "${UNINSTALLER_RES}"
  cp "${UNINSTALLER_REMOVAL}" "${UNINSTALLER_RES}/px3-uninstall.sh"
  install_host_detection "${UNINSTALLER_RES}"
  chmod +x "${UNINSTALLER_RES}/px3-uninstall.sh" "${UNINSTALLER_RES}/detect-au-hosts.sh"

  # Identity, so the removal script matches what this build actually installed.
  {
    echo "PX3_PRODUCT_NAME='${PRODUCT_NAME}'"
    echo "PX3_BUNDLE_ID='${PACKAGE_ID_BASE}'"
  } > "${UNINSTALLER_RES}/px3-identity.env"

  UNINSTALLER_PLIST="${UNINSTALLER_APP_PATH}/Contents/Info.plist"
  /usr/libexec/PlistBuddy -c "Set :CFBundleName PX3 Uninstaller" "${UNINSTALLER_PLIST}" 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString ${PROJECT_VERSION}" "${UNINSTALLER_PLIST}" 2>/dev/null \
    || /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string ${PROJECT_VERSION}" "${UNINSTALLER_PLIST}" 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Set :CFBundleVersion ${PROJECT_VERSION}" "${UNINSTALLER_PLIST}" 2>/dev/null \
    || /usr/libexec/PlistBuddy -c "Add :CFBundleVersion string ${PROJECT_VERSION}" "${UNINSTALLER_PLIST}" 2>/dev/null || true
  # No Dock icon: it is a one-shot utility, not an application to switch to.
  /usr/libexec/PlistBuddy -c "Add :LSUIElement bool true" "${UNINSTALLER_PLIST}" 2>/dev/null || true

  # Its own icon - the app's with a red X through it - falling back to the
  # app's if that has not been generated. This used to be conditional on the
  # installer's branding image being present, which is unrelated: with no
  # branding configured the uninstaller simply had no icon at all.
  if [[ -f "${REPO_ROOT}/Source/Assets/px3-uninstall.icns" ]]; then
    cp "${REPO_ROOT}/Source/Assets/px3-uninstall.icns" "${UNINSTALLER_RES}/applet.icns"
  elif [[ -f "${REPO_ROOT}/Source/Assets/px3.icns" ]]; then
    cp "${REPO_ROOT}/Source/Assets/px3.icns" "${UNINSTALLER_RES}/applet.icns"
  fi

  UNINSTALLER_SIGN_STATE="unsigned"
  if [[ "${SIGN_MODE}" == true && -n "${SIGN_IDENTITY}" ]]; then
    codesign --force --deep --options runtime --timestamp --sign "${SIGN_IDENTITY}" "${UNINSTALLER_APP_PATH}" \
      && UNINSTALLER_SIGN_STATE="signed"

    if [[ "${UNINSTALLER_SIGN_STATE}" == "signed" ]]; then
      notarize_if_requested "${UNINSTALLER_APP_PATH}" "Uninstaller"
      [[ "${NOTARIZE_MODE}" == true ]] && UNINSTALLER_SIGN_STATE="signed + notarised"
    fi
  fi

  # Verify it actually carries what it needs, rather than trusting osacompile.
  for required in px3-uninstall.sh detect-au-hosts.sh au-hosts.tsv; do
    [[ -e "${UNINSTALLER_RES}/${required}" ]] \
      || die "Uninstaller application is missing ${required}"
  done
  [[ -x "${UNINSTALLER_APP_PATH}/Contents/MacOS/applet" ]] \
    || die "Uninstaller application has no executable"
  sh -n "${UNINSTALLER_RES}/px3-uninstall.sh" || die "Bundled removal script is not valid sh"

  cp -R "${UNINSTALLER_APP_PATH}" "${DIST_DIR}/"
  echo "  Uninstaller:   ${UNINSTALLER_APP_PATH} (${UNINSTALLER_SIGN_STATE})"
else
  echo "  Skipped (--no-uninstaller)."
fi

# The archive is built here rather than alongside the other dist copies, so that
# the uninstaller - which only exists by this point - is inside it.
(
  cd "${DIST_ROOT}"
  zip -qry "$(basename "${ZIP_PATH}")" "$(basename "${DIST_DIR}")"
)
[[ -s "${ZIP_PATH}" ]] || die "Distribution archive was not created: ${ZIP_PATH}"

echo "[8/8] Packaging validation"
echo "  AU package:    ${AU_COMPONENT_PKG}"
echo "  VST3 package:  ${VST3_COMPONENT_PKG}"
if [[ -n "${APP_BUNDLE}" ]]; then
  echo "  App package:   ${APP_COMPONENT_PKG}"
fi
echo "  Installer:     ${PKG_PATH} (${PKG_SIGN_STATE})"
if [[ "${BUILD_UNINSTALLER}" == true ]]; then
  echo "  Uninstaller:   ${UNINSTALLER_APP_PATH} (${UNINSTALLER_SIGN_STATE})"
fi

echo

echo "=========================================="
echo "   BUILD SUCCESSFUL"
echo "=========================================="
echo
echo "Version: ${PROJECT_VERSION}"
echo "Architecture: arm64"
echo "Signing: ${SIGNED_STATE}"
echo "Installer Signing: ${PKG_SIGN_STATE}"
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
echo ""
echo "PKG (installer, user selects AU / VST3 / Standalone):"
echo "  ${PKG_PATH}"
if [[ "${BUILD_UNINSTALLER}" == true ]]; then
  echo ""
  echo "APP (uninstaller - removes plugins, factory AND user presets):"
  echo "  ${UNINSTALLER_APP_PATH}"
fi

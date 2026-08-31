#!/usr/bin/env bash
#
# Checks that release artifacts are signed, notarised, stapled and accepted by
# Gatekeeper - for the three things a user actually launches or opens: the
# installer package, the uninstaller app and the standalone app.
#
# Distribution is the one part of a release that cannot be verified by building
# it. A binary that is merely signed installs perfectly on the machine that
# built it and is refused on every other one, because the local machine already
# trusts the developer certificate and a stranger's does not - it wants Apple's
# notarisation ticket. The only way to tell the two cases apart is to check for
# the ticket explicitly, which is what this does.
#
#   ./scripts/test-signing.sh --preflight   credentials only, submits nothing
#   ./scripts/test-signing.sh               verify what is already in dist/
#   ./scripts/test-signing.sh --full        build, sign, notarise, then verify
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_ROOT="${REPO_ROOT}/dist"

MODE="verify"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"

case "${1:-}" in
  --preflight) MODE="preflight" ;;
  --full)      MODE="full" ;;
  --help|-h)   sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  "")          ;;
  *)           echo "Unknown option: $1" >&2; exit 2 ;;
esac

PASS=0
FAIL=0
SKIP=0

# Reports the measured state either way, so a pass says what it verified rather
# than only that it verified something.
check() {
  local name="$1" ok="$2" detail="$3"
  if [[ "${ok}" == true ]]; then
    printf '  ok    %-52s %s\n' "${name}" "${detail}"
    PASS=$((PASS + 1))
  else
    printf '  FAIL  %-52s %s\n' "${name}" "${detail}"
    FAIL=$((FAIL + 1))
  fi
}

skip() {
  printf '  skip  %-52s %s\n' "$1" "$2"
  SKIP=$((SKIP + 1))
}

echo ""
echo "PX3 SIGNING AND NOTARISATION TEST"
echo ""
echo "TOOLING AND CREDENTIALS"
echo ""

for tool in codesign productsign spctl pkgutil ditto; do
  if command -v "${tool}" >/dev/null 2>&1; then
    check "Tool_${tool}IsAvailable" true "$(command -v "${tool}")"
  else
    check "Tool_${tool}IsAvailable" false "not on PATH"
  fi
done

if xcrun notarytool --version >/dev/null 2>&1; then
  check "Tool_notarytoolIsAvailable" true "notarytool $(xcrun notarytool --version 2>&1 | head -n1)"
else
  check "Tool_notarytoolIsAvailable" false "xcrun notarytool is not usable"
fi

if xcrun stapler --help >/dev/null 2>&1 || [[ $? -ne 127 ]]; then
  check "Tool_staplerIsAvailable" true "xcrun stapler responds"
else
  check "Tool_staplerIsAvailable" false "xcrun stapler is not usable"
fi

# Two DIFFERENT certificates are needed and they are not interchangeable:
# "Developer ID Application" signs code, "Developer ID Installer" signs the
# .pkg. Having only the first is the usual reason productsign fails at the very
# end of a release build.
APP_IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null \
  | grep "Developer ID Application:" | head -n1 | sed -E 's/.*"(.*)"/\1/')"
INSTALLER_IDENTITY="$(security find-identity -v 2>/dev/null \
  | grep "Developer ID Installer:" | head -n1 | sed -E 's/.*"(.*)"/\1/')"

if [[ -n "${APP_IDENTITY}" ]]; then
  check "Identity_DeveloperIdApplicationIsInstalled" true "${APP_IDENTITY}"
else
  check "Identity_DeveloperIdApplicationIsInstalled" false \
        "no 'Developer ID Application' certificate in the keychain"
fi

if [[ -n "${INSTALLER_IDENTITY}" ]]; then
  check "Identity_DeveloperIdInstallerIsInstalled" true "${INSTALLER_IDENTITY}"
else
  check "Identity_DeveloperIdInstallerIsInstalled" false \
        "no 'Developer ID Installer' certificate - productsign cannot sign the pkg"
fi

# Both certificates must belong to the SAME team, or the pkg is signed by one
# developer and its contents by another and Apple rejects the submission.
APP_TEAM="$(printf '%s' "${APP_IDENTITY}" | sed -nE 's/.*\(([A-Z0-9]+)\)$/\1/p')"
INSTALLER_TEAM="$(printf '%s' "${INSTALLER_IDENTITY}" | sed -nE 's/.*\(([A-Z0-9]+)\)$/\1/p')"
if [[ -n "${APP_TEAM}" && "${APP_TEAM}" == "${INSTALLER_TEAM}" ]]; then
  check "Identity_BothCertificatesShareOneTeam" true "team ${APP_TEAM}"
else
  check "Identity_BothCertificatesShareOneTeam" false \
        "application team '${APP_TEAM}' vs installer team '${INSTALLER_TEAM}'"
fi

# The certificate being INSTALLED and the certificate being USABLE HERE are
# different claims, and only the second one matters.
#
# codesign needs authorised access to the private key, which needs a security
# session to grant it. A shell without one - anything not launched from the
# user's own Terminal, which is to say most automation - fails with
# `errSecInternalComponent` while `find-identity` still cheerfully reports the
# identity as valid. That cost a full eight-minute release build here before it
# surfaced, at the signing step, after everything had been compiled.
#
# So this signs something. A throwaway file, ad-hoc first to prove codesign
# itself works, then for real.
SIGN_PROBE="$(mktemp -t px3signprobe)"
printf 'probe' > "${SIGN_PROBE}"
if ! codesign --force --sign - "${SIGN_PROBE}" >/dev/null 2>&1; then
  check "Identity_CanActuallySignHere" false "codesign cannot sign at all, even ad-hoc"
elif [[ -z "${APP_IDENTITY}" ]]; then
  check "Identity_CanActuallySignHere" false "no Developer ID Application identity to try"
else
  SIGN_PROBE_OUT="$(codesign --force --options runtime --sign "${APP_IDENTITY}" \
                             "${SIGN_PROBE}" 2>&1)"
  if [[ $? -eq 0 && "${SIGN_PROBE_OUT}" != *"errSec"* ]]; then
    check "Identity_CanActuallySignHere" true "signed a throwaway file with the real identity"
  elif [[ "${SIGN_PROBE_OUT}" == *"errSecInternalComponent"* ]]; then
    check "Identity_CanActuallySignHere" false \
          "errSecInternalComponent - the key exists but this shell has no security session to unlock it. Run the release from your own Terminal, or grant non-interactive access with: security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k <login-password> ~/Library/Keychains/login.keychain-db"
  else
    check "Identity_CanActuallySignHere" false \
          "$(printf '%s' "${SIGN_PROBE_OUT}" | head -n1)"
  fi
fi
rm -f "${SIGN_PROBE}"

# The same question for the INSTALLER certificate, which is a separate identity
# used by a separate tool. Answered by actually signing a throwaway package,
# because "the certificate is installed" did not turn out to mean the pkg would
# be signed: the release script only ever read the installer identity from
# $DEVELOPER_ID_INSTALLER, so on a machine with the certificate present and the
# variable unset it silently produced an unsigned pkg, submitted it, and Apple
# answered "The binary is not signed."
PKG_PROBE_DIR="$(mktemp -d -t px3pkgprobe)"
if ! pkgbuild --identifier com.px3.signprobe --version 1 --nopayload \
              "${PKG_PROBE_DIR}/probe.pkg" >/dev/null 2>&1; then
  check "Identity_CanActuallySignAnInstallerHere" false "pkgbuild could not create a probe package"
elif [[ -z "${INSTALLER_IDENTITY}" ]]; then
  check "Identity_CanActuallySignAnInstallerHere" false "no Developer ID Installer identity to try"
else
  PKG_PROBE_OUT="$(productsign --sign "${INSTALLER_IDENTITY}" \
                               "${PKG_PROBE_DIR}/probe.pkg" \
                               "${PKG_PROBE_DIR}/probe-signed.pkg" 2>&1)"
  if [[ -f "${PKG_PROBE_DIR}/probe-signed.pkg" ]] \
     && pkgutil --check-signature "${PKG_PROBE_DIR}/probe-signed.pkg" 2>/dev/null \
        | grep -q "Developer ID Installer:"; then
    check "Identity_CanActuallySignAnInstallerHere" true \
          "signed a throwaway package with the real installer identity"
  elif [[ "${PKG_PROBE_OUT}" == *"errSecInternalComponent"* ]]; then
    check "Identity_CanActuallySignAnInstallerHere" false \
          "errSecInternalComponent - same cause as the application identity above"
  else
    check "Identity_CanActuallySignAnInstallerHere" false \
          "$(printf '%s' "${PKG_PROBE_OUT}" | head -n1)"
  fi
fi
rm -rf "${PKG_PROBE_DIR}"

# Credentials are proved against Apple rather than merely being present: a
# stored profile whose app-specific password has been revoked looks identical
# to a working one until something is submitted.
NOTARY_AUTH=()
if [[ -n "${NOTARY_PROFILE}" ]]; then
  NOTARY_AUTH=(--keychain-profile "${NOTARY_PROFILE}")
elif [[ -n "${NOTARY_KEY:-}" && -n "${NOTARY_KEY_ID:-}" && -n "${NOTARY_ISSUER:-}" ]]; then
  NOTARY_AUTH=(--key "${NOTARY_KEY}" --key-id "${NOTARY_KEY_ID}" --issuer "${NOTARY_ISSUER}")
elif [[ -n "${APPLE_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]; then
  NOTARY_AUTH=(--apple-id "${APPLE_ID}" --password "${APPLE_APP_PASSWORD}" --team-id "${APPLE_TEAM_ID}")
fi

if [[ ${#NOTARY_AUTH[@]} -eq 0 ]]; then
  check "Notary_CredentialsAreConfigured" false \
        "set NOTARY_PROFILE (see: xcrun notarytool store-credentials), or NOTARY_KEY/NOTARY_KEY_ID/NOTARY_ISSUER, or APPLE_ID/APPLE_APP_PASSWORD/APPLE_TEAM_ID"
else
  # No --limit. notarytool 1.1.2 does not have that option, and passing it made
  # this check fail on every machine with that version while reporting the
  # reason as "Apple rejected these credentials" - a working profile
  # misdiagnosed as a revoked one, which is the most expensive kind of wrong
  # answer a preflight can give.
  #
  # The output is captured so a usage error can be told apart from a refusal.
  # An empty account still authenticates and answers "No submission history",
  # which is a pass: the round-trip is the proof, not the contents.
  NOTARY_OUT="$(xcrun notarytool history "${NOTARY_AUTH[@]}" 2>&1)"
  if [[ "${NOTARY_OUT}" == *"Unknown option"* || "${NOTARY_OUT}" == *"Usage:"* ]]; then
    check "Notary_CredentialsAreAcceptedByApple" false \
          "notarytool rejected the command, not the credentials: $(printf '%s' "${NOTARY_OUT}" | head -n1)"
  elif [[ "${NOTARY_OUT}" == *"No submission history"* ]]; then
    check "Notary_CredentialsAreAcceptedByApple" true \
          "authenticated; no submissions yet"
  elif [[ "${NOTARY_OUT}" == *"createdDate"* || "${NOTARY_OUT}" == *"id: "* ]]; then
    check "Notary_CredentialsAreAcceptedByApple" true "authenticated, history readable"
  else
    check "Notary_CredentialsAreAcceptedByApple" false \
          "Apple rejected these credentials: $(printf '%s' "${NOTARY_OUT}" | head -n1)"
  fi
fi

if [[ "${MODE}" == "preflight" ]]; then
  echo ""
  echo "  ${PASS} passed, ${FAIL} failed, ${SKIP} skipped  (preflight only - nothing was submitted)"
  echo ""
  [[ ${FAIL} -eq 0 ]] || exit 1
  exit 0
fi

if [[ "${MODE}" == "full" ]]; then
  echo ""
  echo "BUILDING, SIGNING AND NOTARISING"
  echo ""
  [[ -n "${NOTARY_PROFILE}" ]] || { echo "  --full needs NOTARY_PROFILE set" >&2; exit 2; }
  "${REPO_ROOT}/scripts/build-release.sh" --notarize --notary-profile "${NOTARY_PROFILE}" \
    || { echo "  release build failed" >&2; exit 1; }
fi

echo ""
echo "ARTIFACTS"
echo ""

PKG_PATH="$(find "${DIST_ROOT}" -maxdepth 1 -name 'PX3-v*.pkg' 2>/dev/null | sort | tail -n1)"
UNINSTALLER_PATH="${DIST_ROOT}/PX3 Uninstaller.app"
APP_PATH="$(find "${DIST_ROOT}" -maxdepth 3 -name '*.app' -path '*Standalone*' 2>/dev/null | sort | tail -n1)"

# A bundle and a package are verified by different tools - codesign understands
# one, pkgutil the other - so they cannot share a code path.
verify_bundle() {
  local path="$1" label="$2"

  if [[ ! -d "${path}" ]]; then
    skip "${label}_IsSigned" "not built: ${path}"
    skip "${label}_IsNotarisedAndStapled" "not built"
    skip "${label}_PassesGatekeeper" "not built"
    return
  fi

  local out
  out="$(codesign --verify --deep --strict --verbose=2 "${path}" 2>&1)"
  if [[ $? -eq 0 ]]; then
    check "${label}_IsSigned" true "satisfies its designated requirement"
  else
    check "${label}_IsSigned" false "$(printf '%s' "${out}" | tail -n1)"
  fi

  # The hardened runtime is a PRECONDITION of notarisation, not a nicety -
  # Apple rejects anything without it, so a missing flag here explains a
  # rejection that otherwise looks unrelated.
  local flags
  flags="$(codesign --display --verbose=2 "${path}" 2>&1 | grep -E '^CodeDirectory' | head -n1)"
  if printf '%s' "${flags}" | grep -q "runtime"; then
    check "${label}_UsesTheHardenedRuntime" true "runtime flag present"
  else
    check "${label}_UsesTheHardenedRuntime" false "no runtime flag: ${flags}"
  fi

  local authority
  authority="$(codesign --display --verbose=4 "${path}" 2>&1 | grep '^Authority=' | head -n1)"
  if printf '%s' "${authority}" | grep -q "Developer ID Application"; then
    check "${label}_IsSignedWithDeveloperId" true "${authority#Authority=}"
  else
    check "${label}_IsSignedWithDeveloperId" false "${authority:-no authority reported}"
  fi

  if xcrun stapler validate "${path}" >/dev/null 2>&1; then
    check "${label}_IsNotarisedAndStapled" true "ticket validates offline"
  else
    check "${label}_IsNotarisedAndStapled" false \
          "no stapled ticket - this installs here and is refused on other machines"
  fi

  out="$(spctl --assess --type execute --verbose=4 "${path}" 2>&1)"
  if printf '%s' "${out}" | grep -q "accepted"; then
    check "${label}_PassesGatekeeper" true "$(printf '%s' "${out}" | grep -E 'source=' | head -n1)"
  else
    check "${label}_PassesGatekeeper" false "$(printf '%s' "${out}" | tail -n1)"
  fi
}

verify_package() {
  local path="$1" label="$2"

  if [[ ! -f "${path}" ]]; then
    skip "${label}_IsSigned" "not built: ${path}"
    skip "${label}_IsNotarisedAndStapled" "not built"
    skip "${label}_PassesGatekeeper" "not built"
    return
  fi

  local out
  out="$(pkgutil --check-signature "${path}" 2>&1)"
  if printf '%s' "${out}" | grep -q "Developer ID Installer"; then
    check "${label}_IsSigned" true \
          "$(printf '%s' "${out}" | grep -E '^\s+1\. ' | head -n1 | sed 's/^ *//')"
  else
    check "${label}_IsSigned" false "$(printf '%s' "${out}" | head -n2 | tail -n1 | sed 's/^ *//')"
  fi

  if xcrun stapler validate "${path}" >/dev/null 2>&1; then
    check "${label}_IsNotarisedAndStapled" true "ticket validates offline"
  else
    check "${label}_IsNotarisedAndStapled" false \
          "no stapled ticket - Gatekeeper will refuse this on a clean machine"
  fi

  # An installer is assessed as "install", not "execute". Assessing a pkg as
  # execute reports a failure that means nothing.
  out="$(spctl --assess --type install --verbose=4 "${path}" 2>&1)"
  if printf '%s' "${out}" | grep -q "accepted"; then
    check "${label}_PassesGatekeeper" true "$(printf '%s' "${out}" | grep -E 'source=' | head -n1)"
  else
    check "${label}_PassesGatekeeper" false "$(printf '%s' "${out}" | tail -n1)"
  fi
}

verify_package "${PKG_PATH:-${DIST_ROOT}/PX3.pkg}" "Installer"
verify_bundle  "${UNINSTALLER_PATH}"               "Uninstaller"
verify_bundle  "${APP_PATH:-${DIST_ROOT}/PX3.app}" "Standalone"

echo ""
echo "  ${PASS} passed, ${FAIL} failed, ${SKIP} skipped"
echo ""
[[ ${FAIL} -eq 0 ]] || exit 1
exit 0

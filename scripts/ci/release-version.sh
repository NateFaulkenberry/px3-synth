#!/usr/bin/env bash
#
# Resolves a release tag into the several versions a release actually needs, and
# refuses to proceed if they disagree with the build.
#
#   ./scripts/ci/release-version.sh v0.7.1-beta.1
#
# Emits KEY=VALUE lines, ready for $GITHUB_OUTPUT:
#
#   tag=v0.7.1-beta.1     the tag as given
#   version=0.7.1-beta.1  tag without its leading v; what filenames carry
#   core=0.7.1            MAJOR.MINOR.PATCH only; what CMake is allowed
#   prerelease=true       whether the tag carries a prerelease suffix
#
# ---------------------------------------------------------------------------
# Why this verifies instead of injecting
# ---------------------------------------------------------------------------
#
# PX3_VERSION in CMakeLists.txt is a plain `set()`, not a cache entry, so it
# OVERRIDES anything passed as -DPX3_VERSION= - silently. A release workflow
# that tried to inject the tag version would produce binaries reporting the old
# version inside files named after the new one, and nothing anywhere would say
# so. Verified, not assumed: `set(PX3_VERSION "0.7.0")` with -DPX3_VERSION=9.9.9
# still yields 0.7.0.
#
# So CMakeLists.txt stays the single source of truth and this checks the tag
# agrees with it. The failure mode becomes "the release stops and tells you to
# bump PX3_VERSION", which is a build that does not happen rather than a build
# that lies.
#
# CMake's SemVer regex also rejects prerelease suffixes, so a beta tag has to be
# split: 0.7.1 goes to the build, 0.7.1-beta.1 goes on the filenames.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CMAKE_FILE="${PX3_CMAKE_FILE:-${REPO_ROOT}/CMakeLists.txt}"

die() { echo "ERROR: $*" >&2; exit 1; }

TAG="${1:-}"
[[ -n "${TAG}" ]] || die "usage: release-version.sh <tag>   (example: v0.7.1-beta.1)"

# vMAJOR.MINOR.PATCH with an optional -prerelease suffix. Anything else is a
# typo, and a typo in a tag becomes a typo in every filename downstream.
if [[ ! "${TAG}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-([0-9A-Za-z.-]+))?$ ]]; then
  die "tag '${TAG}' is not vMAJOR.MINOR.PATCH[-prerelease] (examples: v0.7.1, v0.7.1-beta.1, v0.7.1-rc.1)"
fi

CORE="${BASH_REMATCH[1]}"
SUFFIX="${BASH_REMATCH[3]:-}"
VERSION="${TAG#v}"

PRERELEASE=false
[[ -n "${SUFFIX}" ]] && PRERELEASE=true

# The version the build will actually stamp into the binaries, read the same way
# scripts/build-release.sh reads it.
CMAKE_VERSION="$(grep -E '^[[:space:]]*set\([[:space:]]*PX3_VERSION[[:space:]]+"?[0-9]+\.[0-9]+\.[0-9]+"?[[:space:]]*\)' "${CMAKE_FILE}" \
  | head -n1 | sed -E 's/.*PX3_VERSION[[:space:]]+"?([0-9]+\.[0-9]+\.[0-9]+)"?.*/\1/' || true)"
[[ -n "${CMAKE_VERSION}" ]] || die "could not read PX3_VERSION from ${CMAKE_FILE}"

if [[ "${CMAKE_VERSION}" != "${CORE}" ]]; then
  die "tag ${TAG} means version ${CORE}, but PX3_VERSION in CMakeLists.txt is ${CMAKE_VERSION}.

The binaries would report ${CMAKE_VERSION} while every filename said ${CORE}.
Fix it by bumping PX3_VERSION to ${CORE} on main, then re-tagging:

    1. edit CMakeLists.txt   set(PX3_VERSION \"${CORE}\")
    2. commit and merge to main
    3. delete this release and its tag, then re-create it on the new commit"
fi

cat <<EOF
tag=${TAG}
version=${VERSION}
core=${CORE}
prerelease=${PRERELEASE}
EOF

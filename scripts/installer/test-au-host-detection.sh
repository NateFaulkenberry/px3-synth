#!/usr/bin/env bash
# Regression tests for the Audio Unit host detection used by the installer and
# the uninstaller.
#
# Classification is tested by injecting bundle identifiers through
# PX3_FAKE_BUNDLE_IDS rather than by launching real applications, so the suite
# runs anywhere, offline, in about a second.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DETECT="${SCRIPT_DIR}/detect-au-hosts.sh"

pass=0
fail=0

# expectAllowed <description> <bundle ids>
expectAllowed() {
  local description="$1" ids="$2" output status
  output="$(PX3_FAKE_BUNDLE_IDS="${ids}" "${DETECT}" 2>/dev/null)"
  status=$?
  if [[ ${status} -eq 0 && -z "${output}" ]]; then
    printf "  ok    %s\n" "${description}"
    pass=$((pass + 1))
  else
    printf "  FAIL  %s  (exit %d, reported: %s)\n" "${description}" "${status}" "$(echo "${output}" | tr '\n' ' ')"
    fail=$((fail + 1))
  fi
}

# expectBlocked <description> <bundle ids> <expected host names, comma separated>
expectBlocked() {
  local description="$1" ids="$2" expected="$3" output status actual
  output="$(PX3_FAKE_BUNDLE_IDS="${ids}" "${DETECT}" 2>/dev/null)"
  status=$?
  actual="$(echo "${output}" | sort | tr '\n' ',' | sed 's/,$//')"
  local want
  want="$(echo "${expected}" | tr ',' '\n' | sort | tr '\n' ',' | sed 's/,$//')"
  if [[ ${status} -eq 1 && "${actual}" == "${want}" ]]; then
    printf "  ok    %s  -> %s\n" "${description}" "${actual}"
    pass=$((pass + 1))
  else
    printf "  FAIL  %s  (exit %d, got '%s', wanted '%s')\n" "${description}" "${status}" "${actual}" "${want}"
    fail=$((fail + 1))
  fi
}

echo
echo "AU HOST DETECTION"
echo "================="
echo
echo "No hosts running"
expectAllowed "nothing running at all" ""
expectAllowed "only the Finder" "com.apple.finder"

echo
echo "Known hosts are detected"
expectBlocked "Logic Pro" "com.apple.logic10" "Logic Pro"
expectBlocked "Ableton Live" "com.ableton.live" "Ableton Live"
expectBlocked "REAPER" "com.cockos.reaper" "REAPER"
expectBlocked "Bitwig Studio" "com.bitwig.BitwigStudio" "Bitwig Studio"
expectBlocked "Pro Tools" "com.avid.ProTools" "Pro Tools"
expectBlocked "version-specific id (Cubase 13)" "com.steinberg.cubase13" "Cubase"
expectBlocked "version-specific id (Studio One 6)" "com.presonus.studioone6" "Studio One"
expectBlocked "identifier case is ignored" "COM.APPLE.LOGIC10" "Logic Pro"

echo
echo "Multiple hosts are all reported"
expectBlocked "Logic Pro and Ableton Live" \
  "com.apple.logic10,com.ableton.live" "Logic Pro,Ableton Live"
expectBlocked "three hosts at once" \
  "com.apple.logic10,com.cockos.reaper,com.ableton.live" "Logic Pro,REAPER,Ableton Live"
expectBlocked "a host among ordinary applications" \
  "com.apple.finder,com.apple.Safari,com.apple.logic10,com.spotify.client" "Logic Pro"

echo
echo "False positives - none of these may block an install"
# This is the failure this whole mechanism was rewritten for. macOS runs auval
# by itself after any Audio Unit is installed, and the previous process-name
# check listed it as a host, so the installer blocked itself moments after a
# successful install.
expectAllowed "auval, which macOS runs after every AU install" "com.apple.audio.auval"
expectAllowed "AudioComponentRegistrar" "com.apple.audio.AudioComponentRegistrar"
expectAllowed "coreaudiod and CoreAudio helpers" \
  "com.apple.audio.coreaudiod,com.apple.audio.SandboxHelper,com.apple.audio.audiomxd"
expectAllowed "Safari" "com.apple.Safari"
expectAllowed "Google Chrome" "com.google.Chrome"
expectAllowed "Spotify" "com.spotify.client"
expectAllowed "QuickTime Player" "com.apple.QuickTimePlayerX"
expectAllowed "Discord" "com.hnc.Discord"
expectAllowed "Zoom" "us.zoom.xos"
expectAllowed "Music and Podcasts" "com.apple.Music,com.apple.podcasts"

echo
echo "Unknown applications are never guessed at"
expectAllowed "id containing 'audio'"  "com.example.MyAudioThing"
expectAllowed "id containing 'music'"  "com.example.MusicMaker"
expectAllowed "id containing 'sound'"  "com.example.SoundToolkit"
expectAllowed "id containing 'studio'" "com.example.StudioProX"
expectAllowed "id containing 'daw'"    "com.example.SuperDAW"
expectAllowed "an unrelated app called 'Live'" "com.example.LiveStreamer"
expectAllowed "an unrelated app called 'Reason'" "com.example.ReasonableApp"

echo
echo "Retry sequence"
# The installer checks once up front and again immediately before it touches
# the filesystem, so a host opened while it sat waiting is still caught.
expectAllowed "initial check, nothing open" ""
expectBlocked "host launches, final pre-install check catches it" \
  "com.apple.logic10" "Logic Pro"
expectAllowed "host closed, retry proceeds" ""

echo
echo "Database integrity"
DB="${SCRIPT_DIR}/au-hosts.tsv"
if [[ -f "${DB}" ]]; then
  dupes="$(awk -F'\t' '/^[A-Za-z]/{split($3,a,","); for(i in a) print tolower(a[i])}' "${DB}" \
           | sort | uniq -d)"
  if [[ -z "${dupes}" ]]; then
    printf "  ok    no duplicate bundle identifiers\n"; pass=$((pass + 1))
  else
    printf "  FAIL  duplicate bundle identifiers: %s\n" "$(echo "${dupes}" | tr '\n' ' ')"; fail=$((fail + 1))
  fi

  malformed="$(awk -F'\t' '/^[A-Za-z]/ && NF != 4 {print NR}' "${DB}")"
  if [[ -z "${malformed}" ]]; then
    printf "  ok    every row has all four fields\n"; pass=$((pass + 1))
  else
    printf "  FAIL  malformed rows at lines: %s\n" "$(echo "${malformed}" | tr '\n' ' ')"; fail=$((fail + 1))
  fi

  if ! grep -qiE '^\s*(auval|pluginval)' "${DB}"; then
    printf "  ok    validation tools are not listed as hosts\n"; pass=$((pass + 1))
  else
    printf "  FAIL  a validation tool is listed as a host\n"; fail=$((fail + 1))
  fi
else
  printf "  FAIL  host database missing at %s\n" "${DB}"; fail=$((fail + 1))
fi

echo
echo "------------------------------------------------"
printf "  %d passed, %d failed\n\n" "${pass}" "${fail}"
[[ ${fail} -eq 0 ]] || exit 1

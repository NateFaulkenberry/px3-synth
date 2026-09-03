#!/bin/sh
# What PX3 products are installed on this machine.
#
# Prints one tab-separated record per product:
#
#   <product name>\t<formats>\t<known|unknown>
#
# The uninstaller uses this to build its selection list, so the list describes
# what is ACTUALLY on the machine rather than what this build happens to know
# about. That distinction is the whole point: an uninstaller shipped today has
# to be able to remove a product added next year, and an installation left over
# from a version that predates the product registry.
#
# Two sources, in this order:
#
#   1. px3-products.tsv, written beside this script at release time from the
#      product table in CMakeLists.txt. Authoritative for names and formats.
#   2. Whatever is on disk under the plug-in folders. Anything found here that
#      the manifest does not list is reported as "unknown" and is still
#      removable - it is a PX3 product this build has not heard of.
#
# Needs no privileges: it only reads.

set -u

SELF_DIR=$(CD=$(dirname "$0"); cd "$CD" && pwd)
MANIFEST="${PX3_MANIFEST:-${SELF_DIR}/px3-products.tsv}"

# Overridable so the tests can point the whole scan at a fixture tree.
ROOT="${PX3_SCAN_ROOT:-}"

COMPONENTS_DIRS="${ROOT}/Library/Audio/Plug-Ins/Components"
VST3_DIRS="${ROOT}/Library/Audio/Plug-Ins/VST3"
APPS_DIR="${ROOT}/Applications"

# Per-user plug-in folders count as installed too: a plug-in in ~/Library is
# just as loaded by a DAW as one in /Library, and an uninstaller that ignored
# them would report success while leaving the product working.
user_homes() {
  for dir in "${ROOT}"/Users/*; do
    [ -d "${dir}/Library" ] || continue
    case "$(basename "${dir}")" in
      Shared|Guest|.*) continue ;;
    esac
    echo "${dir}"
  done
}

# A product is present if any of its bundles is. Bundles are collected as raw
# "name<TAB>format" lines and merged in ONE awk pass at the end.
#
# The merge used to happen per bundle, which meant a process per bundle found.
# That is fine in principle and was not fine in practice: on a machine with
# endpoint security inspecting every exec, a fork costs a third of a second,
# and seventeen of them put a nine-second wait in front of the uninstaller's
# first window. One pass, one process.
FOUND_LINES=""

note_found() {
  FOUND_LINES="${FOUND_LINES}$1	$2
"
}

scan_dir() {
  # $1 directory, $2 extension, $3 format tag
  #
  # Shell expansion rather than basename: a plug-in folder with a few thousand
  # bundles in it is entirely normal, and one process per bundle turns this
  # scan from instant into a visible wait.
  [ -d "$1" ] || return 0
  for bundle in "$1"/PX3\ *"$2" "$1"/P\(X3\)*"$2"; do
    [ -e "${bundle}" ] || continue
    base=${bundle##*/}
    base=${base%"$2"}
    note_found "${base}" "$3"
  done
}

scan_dir "${COMPONENTS_DIRS}" ".component" "AU"
scan_dir "${VST3_DIRS}" ".vst3" "VST3"

for home in $(user_homes); do
  scan_dir "${home}/Library/Audio/Plug-Ins/Components" ".component" "AU"
  scan_dir "${home}/Library/Audio/Plug-Ins/VST3" ".vst3" "VST3"
done

# The standalone application, for the products that have one.
if [ -d "${APPS_DIR}" ]; then
  for app in "${APPS_DIR}"/PX3\ *.app; do
    [ -e "${app}" ] || continue
    base=${app##*/}
    note_found "${base%.app}" "App"
  done
fi

[ -n "${FOUND_LINES}" ] || exit 0

# One pass: merge the formats per product, mark each against the manifest, and
# print in the order first seen so the Synth stays at the top of the list.
printf '%s' "${FOUND_LINES}" | awk -F'\t' -v manifest="${MANIFEST}" '
  BEGIN {
    OFS = "\t"
    while ((getline line < manifest) > 0) {
      split(line, f, "\t")
      if (f[2] != "") { known[f[2]] = 1 }
    }
  }
  $1 == "" { next }
  {
    if (!($1 in formats)) { order[++n] = $1; formats[$1] = $2 }
    else if (index(formats[$1], $2) == 0) { formats[$1] = formats[$1] "," $2 }
  }
  END {
    for (i = 1; i <= n; i++) {
      name = order[i]
      print name, formats[name], (name in known ? "known" : "unknown")
    }
  }
'

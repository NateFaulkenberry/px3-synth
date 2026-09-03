#!/bin/sh
# Writes px3-products.tsv - the list of products this build knows about.
#
#   <target>\t<product name>\t<bundle id>\t<formats>\t<standalone yes|no>
#
# Generated from the px3_add_product table in CMakeLists.txt, which is the same
# place scripts/build-product.sh and the installer's component list read from.
# One table, several readers: a product added there is added everywhere, and
# nobody has to remember to update a second list beside it.
#
# Usage: generate-product-manifest.sh <CMakeLists.txt> <output.tsv>

set -eu

CMAKE_FILE="${1:?usage: generate-product-manifest.sh <CMakeLists.txt> <output.tsv>}"
OUTPUT="${2:?usage: generate-product-manifest.sh <CMakeLists.txt> <output.tsv>}"

[ -f "${CMAKE_FILE}" ] || { echo "no such file: ${CMAKE_FILE}" >&2; exit 1; }

awk '
  BEGIN { OFS = "\t"; inProduct = 0 }

  /^px3_add_product\(/ {
    inProduct = 1
    target = ""; name = ""; bundle = ""; formats = ""; standalone = "no"
    next
  }

  inProduct && /^[[:space:]]*TARGET[[:space:]]/       { target = $2; next }
  inProduct && /^[[:space:]]*BUNDLE_ID[[:space:]]/    { bundle = $2; gsub(/"/, "", bundle); next }

  inProduct && /^[[:space:]]*PRODUCT_NAME[[:space:]]/ {
    # PRODUCT_NAME "PX3 Mood" - quoted, and the name has a space in it.
    line = $0
    if (match(line, /"[^"]*"/)) {
      name = substr(line, RSTART + 1, RLENGTH - 2)
    }
    next
  }

  inProduct && /^[[:space:]]*FORMATS[[:space:]]/ {
    for (i = 2; i <= NF; i++) {
      f = $i
      gsub(/\)/, "", f)
      if (f == "") { continue }
      formats = (formats == "" ? f : formats "," f)
      if (f == "Standalone") { standalone = "yes" }
    }
    next
  }

  inProduct && /\)[[:space:]]*$/ {
    if (target != "" && name != "") {
      if (formats == "") { formats = "AU,VST3" }
      print target, name, bundle, formats, standalone
    }
    inProduct = 0
  }
' "${CMAKE_FILE}" > "${OUTPUT}"

[ -s "${OUTPUT}" ] || { echo "no products found in ${CMAKE_FILE}" >&2; exit 1; }

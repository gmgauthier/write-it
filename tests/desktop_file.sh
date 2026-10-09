#!/bin/bash
# SPDX-License-Identifier: Unlicense
#
# The desktop file passes files to the program (%F: one window each) and
# claims every type File > Open accepts, so double-click and Open With reach
# Write-It for each. The extensions come from Open's filter in the source
# (the second argument, src/editor.cpp), so a type added there must be
# added here and to the desktop file too.
# desktop-file-validate runs when installed (CI installs it).

set -u
FILE=$1
SOURCE=${2:-}
failures=0

fail() {
  echo "FAIL: $1"
  failures=$((failures + 1))
}

grep -qx 'Exec=write-it %F' "$FILE" || fail "Exec is not 'write-it %F'"
mime=$(sed -n 's/^MimeType=//p' "$FILE")
has_type() {
  case ";$mime" in
    *";$1;"*) return 0 ;;
    *) return 1 ;;
  esac
}

# The MIME types for each extension Open's filter accepts.
types_for() {
  case "$1" in
    rtf) echo "application/rtf text/rtf" ;;
    md | markdown) echo "text/markdown" ;;
    txt) echo "text/plain" ;;
    *) echo "" ;;
  esac
}

if [ -n "$SOURCE" ]; then
  exts=$(sed -n '/^void MainWindow::open_document/,/^}/p' "$SOURCE" |
    sed -n 's/.*add_pattern("\*\.\([A-Za-z0-9]*\)").*/\1/p' | tr 'A-Z' 'a-z' | sort -u)
  [ -n "$exts" ] || fail "found no extensions in Open's filter in $SOURCE"
  for ext in $exts; do
    types=$(types_for "$ext")
    [ -n "$types" ] || fail "Open accepts .$ext, but this test has no MIME type for it"
    for type in $types; do
      has_type "$type" || fail "Open accepts .$ext, but MimeType lacks $type"
    done
  done
else
  fail "no source file given to read Open's filter from"
fi

if command -v desktop-file-validate >/dev/null; then
  out=$(desktop-file-validate "$FILE" 2>&1) || fail "desktop-file-validate: $out"
  [ -z "$out" ] || fail "desktop-file-validate: $out"
elif [ "${WRITE_IT_SMOKE_REQUIRED:-0}" = 1 ]; then
  fail "desktop-file-validate is not installed"
else
  echo "desktop-file: desktop-file-validate not installed, not run"
fi

if [ "$failures" -gt 0 ]; then
  echo "desktop-file: $failures failed"
  exit 1
fi
echo "desktop-file: ok"

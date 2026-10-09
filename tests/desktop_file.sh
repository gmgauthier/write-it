#!/bin/bash
# SPDX-License-Identifier: Unlicense
#
# The desktop file passes files to the program (%F: one window each) and
# claims RTF and Markdown, so double-click and Open With reach Write-It.
# desktop-file-validate runs when installed (CI installs it).

set -u
FILE=$1
failures=0

fail() {
  echo "FAIL: $1"
  failures=$((failures + 1))
}

grep -qx 'Exec=write-it %F' "$FILE" || fail "Exec is not 'write-it %F'"
mime=$(sed -n 's/^MimeType=//p' "$FILE")
for type in application/rtf text/rtf text/markdown; do
  case ";$mime" in
    *";$type;"*) ;;
    *) fail "MimeType lacks $type" ;;
  esac
done

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

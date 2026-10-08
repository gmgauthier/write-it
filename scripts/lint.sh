#!/bin/sh
# Format and lint src/. Check-only by default (CI). Pass --fix to apply clang-format.
#
#   ./scripts/lint.sh          # gate: SPDX, tabs, clang-format --dry-run, cppcheck
#   ./scripts/lint.sh --fix    # rewrite src/ with clang-format, then the same checks
#
# Requires: clang-format 19, cppcheck. Debian/Devuan:
#   sudo apt install clang-format cppcheck

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

FIX=0
for arg in "$@"; do
  case "$arg" in
    --fix) FIX=1 ;;
    -h|--help)
      echo "usage: $0 [--fix]"
      exit 0
      ;;
    *)
      echo "unknown argument: $arg" >&2
      echo "usage: $0 [--fix]" >&2
      exit 2
      ;;
  esac
done

if [ ! -d src ]; then
  echo "lint: no src/ directory" >&2
  exit 1
fi

need() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "lint: missing tool: $1" >&2
    echo "Debian/Devuan: sudo apt install clang-format cppcheck" >&2
    exit 1
  fi
}

need clang-format
need cppcheck

# Prefer a 19.x binary when several are installed (CI pins 19.1.7).
if command -v clang-format-19 >/dev/null 2>&1; then
  CLANG_FORMAT=clang-format-19
else
  CLANG_FORMAT=clang-format
fi

echo "lint: $($CLANG_FORMAT --version | tr '\n' ' ')"
echo "lint: $(cppcheck --version)"

status=0
nfiles=0

# Portable: no mapfile. Paths have no spaces in these trees.
files=""
for f in $(find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.cc' -o -name '*.h' \) | sort); do
  nfiles=$((nfiles + 1))
  files="$files $f"

  if ! grep -q 'SPDX-License-Identifier: Unlicense' "$f"; then
    echo "lint: missing SPDX header: $f" >&2
    status=1
  fi
  if grep -q "$(printf '\t')" "$f"; then
    echo "lint: tab characters: $f" >&2
    status=1
  fi
done

if [ "$nfiles" -eq 0 ]; then
  echo "lint: no C++ files under src/" >&2
  exit 1
fi

if [ "$FIX" -eq 1 ]; then
  # shellcheck disable=SC2086
  $CLANG_FORMAT -i $files
  echo "lint: formatted $nfiles files"
else
  # shellcheck disable=SC2086
  if ! $CLANG_FORMAT --dry-run --Werror $files; then
    echo "lint: clang-format failed (run ./scripts/lint.sh --fix)" >&2
    status=1
  fi
fi

# Always check-only. unknownMacro: VERSION/APP_ID live in generated config.hpp.
if ! cppcheck --enable=warning --std=c++17 --language=c++ \
    --error-exitcode=1 --inline-suppr --quiet \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --suppress=unmatchedSuppression \
    --suppress=unknownMacro \
    --suppress=normalCheckLevelMaxBranches \
    src; then
  echo "lint: cppcheck failed" >&2
  status=1
fi

if [ "$status" -ne 0 ]; then
  echo "lint: FAILED" >&2
  exit "$status"
fi

echo "lint: ok ($nfiles files)"
exit 0

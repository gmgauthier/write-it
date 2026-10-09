#!/bin/bash
# SPDX-License-Identifier: Unlicense
#
# Launches the built write-it with files on the command line, as a desktop
# file's %F or a terminal would, and checks what opens. Needs an X display
# (CI runs the suite under xvfb-run), xwininfo, and dbus-run-session: every
# case runs on a private session bus, so GApplication's single instance is
# real but shared with nothing else on the machine.
#
# Exits 77 (skipped) without those tools, unless WRITE_IT_SMOKE_REQUIRED=1.

set -u
BIN=$(readlink -f "$1")

skip() {
  echo "smoke-open: skipped: $1"
  if [ "${WRITE_IT_SMOKE_REQUIRED:-0}" = 1 ]; then
    exit 1
  fi
  exit 77
}

[ -n "${DISPLAY:-}" ] || skip "no DISPLAY"
command -v xwininfo >/dev/null || skip "no xwininfo (x11-utils)"
command -v dbus-run-session >/dev/null || skip "no dbus-run-session (dbus-daemon)"
if [ -z "${WRITE_IT_SMOKE_BUS:-}" ]; then
  WRITE_IT_SMOKE_BUS=1 exec dbus-run-session -- "$0" "$@"
fi

WORK=$(mktemp -d /tmp/write-it-smoke-XXXXXX)
DOCS="$WORK/docs"
mkdir -p "$DOCS"
failures=0
pids=()

fail() {
  echo "FAIL: $1"
  failures=$((failures + 1))
}

cleanup() {
  for pid in "${pids[@]}"; do
    kill "$pid" 2>/dev/null
  done
  wait 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

rtf() {
  printf '{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0 %s\\par}\n' "$2" >"$1"
}
rtf "$DOCS/a.rtf" "Alpha"
rtf "$DOCS/b.rtf" "Bravo"
rtf "$DOCS/c.rtf" "Charlie"
rtf "$DOCS/a b é.rtf" "Spaces and accents"
printf '# Notes\n\nSome *text*.\n' >"$DOCS/notes.md"
printf 'Plain line one.\nPlain line two.\n' >"$DOCS/plain.txt"

# Every top-level window title on the display, one per line.
titles() {
  xwininfo -root -tree | sed -n 's/^ *0x[0-9a-f]* "\(.*\)": .*/\1/p'
}

has_title() {
  titles | grep -Fxq -- "$1"
}

# Waits up to 15 s for a window titled $1.
wait_title() {
  for _ in $(seq 150); do
    has_title "$1" && return 0
    sleep 0.1
  done
  return 1
}

count_title() {
  titles | grep -Fxc -- "$1"
}

# Starts a primary instance in its own config dir: start NAME [ARGS...].
# Sets PID and LOG.
start() {
  local name=$1
  shift
  mkdir -p "$WORK/$name/config"
  LOG="$WORK/$name/log"
  (cd "$DOCS" && exec env XDG_CONFIG_HOME="$WORK/$name/config" GTK_A11Y=none "$BIN" "$@") \
    >"$LOG" 2>&1 &
  PID=$!
  pids+=("$PID")
}

stop() {
  kill "$PID" 2>/dev/null
  wait "$PID" 2>/dev/null
}

alive() {
  kill -0 "$PID" 2>/dev/null
}

no_refusal() {
  if grep -q "can not open files" "$1"; then
    fail "$2: GLib said it can not open files"
  fi
}

recent_has() {
  grep -F -- "$2" "$WORK/$1/config/write-it/write-it.ini" >/dev/null 2>&1
}

# 1. A relative path, with spaces and accents, opens in the first window.
start relative "a b é.rtf"
wait_title "Write-It - a b é.rtf" || fail "relative: no window titled 'Write-It - a b é.rtf'"
alive || fail "relative: the program exited"
no_refusal "$LOG" relative
recent_has relative "$DOCS/a b é.rtf" || fail "relative: not in recent files"
stop

# 2. Several files: one window each, and all in recent files.
start several "$DOCS/a.rtf" "$DOCS/b.rtf"
wait_title "Write-It - a.rtf" || fail "several: no window for a.rtf"
wait_title "Write-It - b.rtf" || fail "several: no window for b.rtf"
has_title "Write-It - Untitled" && fail "several: an empty Untitled window was left"
recent_has several "$DOCS/a.rtf" || fail "several: a.rtf not in recent files"
recent_has several "$DOCS/b.rtf" || fail "several: b.rtf not in recent files"
no_refusal "$LOG" several
stop

# 3. A second launch hands its file to the running instance and exits 0.
start second
wait_title "Write-It - Untitled" || fail "second: no first window"
(cd "$DOCS" && env XDG_CONFIG_HOME="$WORK/second/config" GTK_A11Y=none timeout 20 \
  "$BIN" "file://$DOCS/c.rtf") >"$WORK/second/remote.log" 2>&1
status=$?
[ "$status" = 0 ] || fail "second: the second launch exited $status"
no_refusal "$WORK/second/remote.log" second
wait_title "Write-It - c.rtf" || fail "second: c.rtf did not open in the running instance"
# The empty first window took it: no Untitled left over.
has_title "Write-It - Untitled" && fail "second: the empty Untitled was not reused"
# A third launch opens beside c.rtf, never in its place.
(cd "$DOCS" && env XDG_CONFIG_HOME="$WORK/second/config" GTK_A11Y=none timeout 20 \
  "$BIN" notes.md) >"$WORK/second/remote2.log" 2>&1
status=$?
[ "$status" = 0 ] || fail "second: the third launch exited $status"
wait_title "Write-It - notes.md" || fail "second: notes.md did not open"
has_title "Write-It - c.rtf" || fail "second: c.rtf's window was replaced"
# Asking again for a file already open brings it forward: no second window.
(cd "$DOCS" && env XDG_CONFIG_HOME="$WORK/second/config" GTK_A11Y=none timeout 20 \
  "$BIN" c.rtf) >"$WORK/second/remote3.log" 2>&1
sleep 1
[ "$(count_title "Write-It - c.rtf")" = 1 ] || fail "second: c.rtf opened twice"
alive || fail "second: the running instance exited"
stop

# 4. Plain text imports, as File > Open does.
start plain plain.txt
wait_title "Write-It - plain.txt" || fail "plain: plain.txt did not open"
stop

# 5. A missing file: the program stays up with one usable window and the
# error dialog (titled Write-It); no exit, no extra window.
start missing "$DOCS/missing.rtf"
wait_title "Write-It - Untitled" || fail "missing: no window"
wait_title "Write-It" || fail "missing: no error dialog"
sleep 0.5
alive || fail "missing: the program exited"
[ "$(count_title "Write-It - Untitled")" = 1 ] || fail "missing: not exactly one window"
no_refusal "$LOG" missing
stop

# 6. A missing file beside a good one: only the good one's window stays.
start mixed "$DOCS/missing.rtf" "$DOCS/a.rtf"
wait_title "Write-It - a.rtf" || fail "mixed: a.rtf did not open"
# The error dialog is up; the missing file's window closes once it is
# dismissed, which a headless run cannot do, so only check nothing else.
alive || fail "mixed: the program exited"
stop

# 7. No local path: refused, the program stays up with a window.
start remote "sftp://example.invalid/x.rtf"
wait_title "Write-It - Untitled" || fail "remote: no window"
wait_title "Write-It" || fail "remote: no refusal dialog"
alive || fail "remote: the program exited"
no_refusal "$LOG" remote
stop

if [ "$failures" -gt 0 ]; then
  echo "smoke-open: $failures failed"
  for f in "$WORK"/*/log "$WORK"/*/remote*.log; do
    [ -s "$f" ] && { echo "--- $f"; cat "$f"; }
  done
  exit 1
fi
echo "smoke-open: ok"

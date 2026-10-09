#!/bin/bash
# SPDX-License-Identifier: Unlicense
#
# Launches the built write-it with files on the command line, as a desktop
# file's %F or a terminal would, and checks what opens. Everything runs on
# this script's own Xvfb and its own private session bus, so GApplication's
# single instance is real but shared with nothing else on the machine, and
# no other program's windows are counted. Needs Xvfb, xwininfo and
# dbus-run-session.
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

command -v Xvfb >/dev/null || skip "no Xvfb"
command -v xwininfo >/dev/null || skip "no xwininfo (x11-utils)"
command -v dbus-run-session >/dev/null || skip "no dbus-run-session (dbus-daemon)"
# Always on a private session bus: a launch on a shared bus would hand its
# files to any Write-It already running there, someone else's included.
if [ -z "${WRITE_IT_SMOKE_OUTER_BUS+set}" ]; then
  export WRITE_IT_SMOKE_OUTER_BUS="${DBUS_SESSION_BUS_ADDRESS:-none}"
  exec dbus-run-session -- "$0" "$@"
fi
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ] ||
  [ "$DBUS_SESSION_BUS_ADDRESS" = "$WRITE_IT_SMOKE_OUTER_BUS" ]; then
  echo "smoke-open: not on a private session bus; refusing to launch write-it"
  exit 1
fi

WORK=$(mktemp -d /tmp/write-it-smoke-XXXXXX)
DOCS="$WORK/docs"
mkdir -p "$DOCS"
failures=0
pids=()

# Our own X server. -displayfd has Xvfb choose a free display number itself,
# so two runs at once never share one. -noreset keeps it from resetting each
# time its last client goes: each case stops the program before starting the
# next, and a launch during a reset fails with "cannot open display".
Xvfb -displayfd 9 -screen 0 1280x900x24 -nolisten tcp -noreset 9>"$WORK/display" >/dev/null 2>&1 &
XVFB=$!
for _ in $(seq 100); do
  [ -s "$WORK/display" ] && break
  sleep 0.1
done
if [ ! -s "$WORK/display" ]; then
  echo "smoke-open: Xvfb did not start"
  kill "$XVFB" 2>/dev/null
  rm -rf "$WORK"
  exit 1
fi
export DISPLAY=":$(head -n 1 "$WORK/display")"

fail() {
  echo "FAIL: $1"
  echo "  windows now: $(titles | tr '\n' '|')"
  failures=$((failures + 1))
}

cleanup() {
  for pid in "${pids[@]}"; do
    kill "$pid" 2>/dev/null
  done
  kill "$XVFB" 2>/dev/null
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
# The first instance is started here, on this script's private bus, so the
# second launch can only reach it; c.rtf's title appearing on this display
# proves it did.
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
# An import is an unsaved document, as from File > Open: hence the " *".
wait_title "Write-It - notes.md *" || fail "second: notes.md did not open"
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
wait_title "Write-It - plain.txt *" || fail "plain: plain.txt did not open"
# An import has no save path, but asking for it again must find its window,
# not import a second copy.
(cd "$DOCS" && env XDG_CONFIG_HOME="$WORK/plain/config" GTK_A11Y=none timeout 20 \
  "$BIN" plain.txt) >"$WORK/plain/remote.log" 2>&1
sleep 1
[ "$(count_title "Write-It - plain.txt *")" = 1 ] || fail "plain: plain.txt opened twice"
alive || fail "plain: the program exited"
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

# 6. A missing file before a good one: the good one opens first, then the
# error. (The missing file's window closes when the error is dismissed,
# which a headless run cannot do.)
start mixed "$DOCS/missing.rtf" "$DOCS/a.rtf"
wait_title "Write-It - a.rtf" || fail "mixed: a.rtf did not open"
wait_title "Write-It" || fail "mixed: no error dialog"
alive || fail "mixed: the program exited"
stop

# 7. No local path: refused, the program stays up with a window.
start remote "sftp://example.invalid/x.rtf"
wait_title "Write-It - Untitled" || fail "remote: no window"
wait_title "Write-It" || fail "remote: no refusal dialog"
alive || fail "remote: the program exited"
no_refusal "$LOG" remote
stop

# 8. The same file by another name: a symlink, a hard link and a ".." path
# to an open file, in later launches, bring its window forward. No second
# window, under any of the names.
ln -s a.rtf "$DOCS/link.rtf"
ln "$DOCS/a.rtf" "$DOCS/hard.rtf"
mkdir -p "$DOCS/sub"
start alias "$DOCS/a.rtf"
wait_title "Write-It - a.rtf" || fail "alias: a.rtf did not open"
n=0
for name in link.rtf hard.rtf sub/../a.rtf; do
  n=$((n + 1))
  (cd "$DOCS" && env XDG_CONFIG_HOME="$WORK/alias/config" GTK_A11Y=none timeout 20 \
    "$BIN" "$name") >"$WORK/alias/remote$n.log" 2>&1
  status=$?
  [ "$status" = 0 ] || fail "alias: the launch of $name exited $status"
done
sleep 1
[ "$(count_title "Write-It - a.rtf")" = 1 ] || fail "alias: a.rtf is not in exactly one window"
has_title "Write-It - link.rtf" && fail "alias: link.rtf opened a second window"
has_title "Write-It - hard.rtf" && fail "alias: hard.rtf opened a second window"
alive || fail "alias: the running instance exited"
stop

if [ "$failures" -gt 0 ]; then
  echo "smoke-open: $failures failed"
  for f in "$WORK"/*/log "$WORK"/*/remote*.log; do
    [ -s "$f" ] && { echo "--- $f"; cat "$f"; }
  done
  exit 1
fi
echo "smoke-open: ok"

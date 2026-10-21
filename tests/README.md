# Typing bench

`tests/bench_key.cpp` measures the typing-speed gate in `DEVELOPMENT.md`. It is the program `write-it-bench-key`. `meson test` does not run it, and neither does the package build.

`bench.sh` from an earlier session is gone. This program follows the procedure that file named: a release build, `plain1000.rtf` and `list1000.rtf`, 50 keys in paragraph 500, the median of three runs, in process CPU time.

## Build

From the repository root:

```
meson setup build-release --buildtype=release
meson compile -C build-release write-it-bench-key
```

A debug build is not the measurement the gate names.

## Run

The bench opens a window. On this i3/Regolith desktop a normal launch tiles and fills the screen. Run it under Xvfb:

```
xvfb-run -a ./build-release/write-it-bench-key
```

A path argument measures that file with the same caret, key count, and median. That result is not the gate. A `.txt` path opens as plain text.

The same commands on another machine give that machine's number. A 4-vCPU LCOS guest is a useful second run. Build there too. Do not copy the host binary.

The program writes the two RTF files in a temporary directory, opens them, and deletes them. It sets `XDG_CONFIG_HOME` to that directory, so it does not read or write `~/.config/write-it`.

## What the numbers mean

Each file is 1000 paragraphs of one sentence, the second file as a numbered list. The caret is at the end of paragraph 500. Each timed key is its own undo step.

A run line has three times, each the median of the 50 keys:

- handler: CPU time inside the key itself
- with refresh: that time plus draining pending main-loop work, including list layout, with no sleep
- wall: the same span on the clock

The gate line is the median of the three run medians, refresh included. The gate in the spec is under 50 ms of CPU.

A burst line types 50 keys that may share one undo step. That line is not the gate.

CPU time is `CLOCK_PROCESS_CPUTIME_ID`. The lost script's timer is unknown, and this one does not wait on the frame clock.

## One pair of runs

Release build of 1.0.8, on 2026-10-11.

| Machine | plain1000 | list1000 |
| --- | --- | --- |
| Plato, i7-12700T, 20 threads | 4.35 ms | 6.93 ms |
| LCOS 0.9 guest, 4 vCPUs of that chip, 8 GB | 4.63 ms | 7.31 ms |

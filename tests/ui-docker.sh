#!/bin/sh
# SPDX-License-Identifier: Unlicense
#
# Desktop suite in a local container. CI does not run these tests.
# Usage: ./tests/ui-docker.sh [image]
# Default image: public.ecr.aws/docker/library/debian:trixie
# Devuan: ./tests/ui-docker.sh devuan/devuan:excalibur
#
# On the host, without Docker:
#   xvfb-run -a meson test -C build --suite ui

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=${1:-public.ecr.aws/docker/library/debian:trixie}

exec docker run --rm \
  -v "$ROOT":/src:ro \
  -w /tmp \
  -e DEBIAN_FRONTEND=noninteractive \
  "$IMAGE" \
  sh -c '
    set -eu
    apt-get update
    apt-get install -y --no-install-recommends \
      ca-certificates meson ninja-build g++ pkg-config \
      libgtkmm-3.0-dev zlib1g-dev \
      xvfb xauth x11-utils dbus-daemon desktop-file-utils
    meson setup /tmp/build /src
    meson compile -C /tmp/build
    xvfb-run -a meson test -C /tmp/build --suite ui --print-errorlogs
  '

#!/bin/sh
# Build release artifacts: source tarball, amd64 .deb, and AppImage.
# Usage: ./scripts/release.sh [tarball|deb|appimage|all]
#
# The AppImage's name is the desktop file's Name= (Write-It).

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

VERSION=$(sed -n "s/^  version: '\\(.*\\)',/\\1/p" meson.build | head -1)
DISTDIR="${ROOT}/dist"
JOB=${1:-all}

mkdir -p "$DISTDIR"

need_build() {
  if [ ! -f "${ROOT}/build/build.ninja" ]; then
    meson setup "${ROOT}/build" "$ROOT"
  fi
}

do_tarball() {
  need_build
  meson dist -C "${ROOT}/build" --no-tests --allow-dirty
  src="${ROOT}/build/meson-dist/write-it-${VERSION}.tar.xz"
  if [ -f "$src" ]; then
    cp -f "$src" "$DISTDIR/"
    echo "tarball: ${DISTDIR}/write-it-${VERSION}.tar.xz"
  else
    echo "meson dist did not produce write-it-${VERSION}.tar.xz" >&2
    ls -la "${ROOT}/build/meson-dist" >&2 || true
    exit 1
  fi
}

do_deb() {
  dpkg-buildpackage -us -uc -b --no-sign
  moved=0
  for f in "${ROOT}/../write-it_${VERSION}"-*.deb \
           "${ROOT}/../write-it-dbgsym_${VERSION}"-*.deb; do
    [ -e "$f" ] || continue
    mv -f "$f" "$DISTDIR/"
    echo "deb: ${DISTDIR}/$(basename "$f")"
    case $(basename "$f") in
      write-it-dbgsym_*) ;;
      *) moved=1 ;;
    esac
  done
  for f in "${ROOT}/../write-it_${VERSION}"-*.buildinfo \
           "${ROOT}/../write-it_${VERSION}"-*.changes; do
    [ -e "$f" ] || continue
    mv -f "$f" "$DISTDIR/"
  done
  if [ "$moved" -ne 1 ]; then
    echo "dpkg-buildpackage did not produce write-it_${VERSION}-*.deb" >&2
    exit 1
  fi
}

do_appimage() {
  if ! command -v linuxdeploy >/dev/null 2>&1; then
    echo "linuxdeploy is not on PATH, so the AppImage was not built." >&2
    exit 1
  fi
  APPDIR="${ROOT}/build/AppDir"
  rm -rf "$APPDIR"
  rm -f "${ROOT}"/*.AppImage "${ROOT}/build"/*.AppImage
  meson setup "${ROOT}/build-appimage" "$ROOT" --prefix=/usr
  meson compile -C "${ROOT}/build-appimage"
  DESTDIR="$APPDIR" meson install -C "${ROOT}/build-appimage"

  export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
  export APPIMAGE_EXTRACT_AND_RUN=1
  PLUGIN_ARGS=""
  if command -v linuxdeploy-plugin-gtk >/dev/null 2>&1 || \
     [ -x "${ROOT}/scripts/linuxdeploy-plugin-gtk.sh" ]; then
    PLUGIN_ARGS="--plugin gtk"
    if [ -x "${ROOT}/scripts/linuxdeploy-plugin-gtk.sh" ]; then
      export PATH="${ROOT}/scripts:${PATH}"
    fi
  fi
  # shellcheck disable=SC2086
  linuxdeploy --appdir "$APPDIR" \
    --executable "${APPDIR}/usr/bin/write-it" \
    --desktop-file "${APPDIR}/usr/share/applications/write-it.desktop" \
    --icon-file "${APPDIR}/usr/share/icons/hicolor/scalable/apps/write-it.svg" \
    $PLUGIN_ARGS \
    --output appimage

  found=""
  for f in "${ROOT}/Write-It-${VERSION}"-*.AppImage \
           "${ROOT}/write-it-${VERSION}"-*.AppImage \
           "${ROOT}/build/Write-It-${VERSION}"-*.AppImage \
           "${ROOT}/build/write-it-${VERSION}"-*.AppImage \
           "${ROOT}"/*.AppImage \
           "${ROOT}/build"/*.AppImage; do
    [ -e "$f" ] || continue
    found=$f
    break
  done
  if [ -z "$found" ]; then
    echo "linuxdeploy did not produce an AppImage" >&2
    exit 1
  fi
  image="${DISTDIR}/Write-It-${VERSION}-x86_64.AppImage"
  mv -f "$found" "$image"
  chmod +x "$image"
  echo "appimage: $image"
}

case "$JOB" in
  tarball) do_tarball ;;
  deb)     do_deb ;;
  appimage) do_appimage ;;
  all)
    do_tarball
    do_deb
    do_appimage
    ;;
  *)
    echo "Usage: $0 [tarball|deb|appimage|all]" >&2
    exit 2
    ;;
esac

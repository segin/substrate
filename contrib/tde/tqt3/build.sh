#!/bin/sh
# contrib/tde/tqt3/build.sh — cross-compile TQt3 for substrate.
#
# TQt3 keeps the classic Qt3 build system: configure builds the host
# qmake/moc/uic, then the libraries are compiled for the -xplatform
# target.  We use a substrate-g++ mkspec (cross gcc + substrate sysroot)
# as the cross target and linux-g++ (host gcc) as the build platform.
#
# Status: WORK IN PROGRESS.  See ../README.SUBSTRATE.md for the porting
# roadmap and the list of substrate-specific issues still open (the X11
# extension headers — Xrandr/Xinerama/Xcursor/Xft — are the current
# blocker; configure them off or stage the headers).
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
VERSION="14.1.6"
TREE="${HERE}/build/tqt-trinity-${VERSION}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE}"
export TQTDIR="${TREE}"
export PKG_CONFIG_PATH=""   # never probe host pkg-config for X

echo "==> configure (host=linux-g++, target=substrate-g++)"
yes yes | ./configure \
    -platform  linux-g++ \
    -xplatform substrate-g++ \
    -prefix /opt/trinity \
    -thread -no-exceptions \
    -no-xrandr -no-xinerama -no-xcursor -no-xft -no-xkb \
    -no-cups -no-nis -no-sm \
    -qt-gif -qt-libpng -qt-libjpeg -qt-zlib

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> Done.  Libraries under ${TREE}/lib"

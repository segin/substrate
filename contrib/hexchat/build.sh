#!/bin/sh
# contrib/hexchat/build.sh — cross-compile HexChat 2.10.2 (GTK+ 2 IRC client).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.10.2"; TREE_DIR="${HERE}/build/hexchat-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-hexchat}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

substrate_sysroot "${HERE}/build/sysroot" \
    glib2 libffi zlib freetype libpng expat fontconfig harfbuzz fribidi \
    pixman cairo pango atk gdk-pixbuf gtk2 \
    xorgproto libXau xtrans libxcb libX11 libXext libXrender
export PYTHON=python3
export LDFLAGS="${LDFLAGS} -ldl"
export CFLAGS="-march=i486 -mtune=i486 -O2 -g -std=gnu11 \
  -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
  -Wno-error=implicit-function-declaration -Wno-error=return-mismatch -Wno-error=format \
  -Wno-error=deprecated-declarations"

substrate_libtool_fix "${TREE_DIR}/configure"
# HexChat hardcodes source-relative paths for its generators
# (src/common/make-te reads textevents.in; src/fe-gtk reads ../../data/*.xml),
# so build IN-SOURCE.  The host glib-compile-resources (on PATH) handles the
# GResource step.
cd "${TREE_DIR}"
make distclean >/dev/null 2>&1 || true
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include --sysconfdir=/etc \
    --enable-gtkfe --enable-openssl --enable-ipv6 \
    --disable-gtktest --disable-glibtest \
    --disable-dbus --disable-libnotify --disable-libproxy \
    --disable-perl --disable-python --disable-plugin \
    --disable-libcanberra --disable-isocodes --disable-nls --disable-sysinfo \
    --disable-textfe \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc
# make-te is a build-time text generator (noinst program) that the cross
# build compiles for the target (can't run on the host) and that reads
# textevents.in from $(srcdir).  Host-build it (self-contained C, no deps)
# and point the rule at $(srcdir) so the generation step runs.
( cd "${TREE_DIR}/src/common"
  gcc -O2 -w -c make-te.c -o make-te.o
  gcc -O2 -w -o make-te make-te.o
  touch make-te.o make-te )
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> hexchat staged at ${DESTDIR}"

#!/bin/sh
# contrib/libogg/build.sh — cross-build libogg for substrate.
# Configured as a linux host (CC stays the substrate cross gcc) so the bundled
# libtool emits a shared library; the output is a substrate binary (OSABI 0x40).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; LIB="libogg"; VERSION="1.3.5"
TREE="${HERE}/build/libogg-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
rm -rf "${BS}"; mkdir -p "${BS}"; cd "${BS}"
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr --enable-shared --enable-static \
  CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la
for so in "${DESTDIR}"/usr/lib/*.so.*; do [ -f "$so" ] && case "$so" in *.so.*.*) printf '\100' | dd of="$so" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null;; esac; done
# mirror to the cross sysroot so dependent ports (vorbis/opus/speex) find it
mkdir -p "${SR}/lib/pkgconfig"
cp -a "${DESTDIR}"/usr/lib/libogg.* "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/include/ogg "${SR}/include/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/ogg.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true
echo "==> ${LIB} staged under ${DESTDIR}"

#!/bin/sh
# contrib/libvorbis/build.sh — cross-build libvorbis for substrate.
# Configured as a linux host (CC stays the substrate cross gcc) so the bundled
# libtool emits a shared library; the output is a substrate binary (OSABI 0x40).
# Depends on libogg, already staged in the cross sysroot.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; LIB="libvorbis"; VERSION="1.3.7"
TREE="${HERE}/build/libvorbis-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
# Let configure find libogg (staged in the cross sysroot) via pkg-config + flags.
export PKG_CONFIG_LIBDIR="${SR}/lib/pkgconfig"
export CPPFLAGS="-I${SR}/include"
export LDFLAGS="-L${SR}/lib"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
rm -rf "${BS}"; mkdir -p "${BS}"; cd "${BS}"
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr --enable-shared --enable-static \
  --with-ogg="${SR}" --with-ogg-libraries="${SR}/lib" --with-ogg-includes="${SR}/include" \
  CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la
for so in "${DESTDIR}"/usr/lib/*.so.*; do [ -f "$so" ] && case "$so" in *.so.*.*) printf '\100' | dd of="$so" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null;; esac; done
# mirror to the cross sysroot so dependent ports (PsyMP3) find it
mkdir -p "${SR}/lib/pkgconfig" "${SR}/include/vorbis"
cp -a "${DESTDIR}"/usr/lib/libvorbis.*     "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/lib/libvorbisenc.*  "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/lib/libvorbisfile.* "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/include/vorbis/.    "${SR}/include/vorbis/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/vorbis.pc     "${SR}/lib/pkgconfig/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/vorbisenc.pc  "${SR}/lib/pkgconfig/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/vorbisfile.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true
echo "==> ${LIB} staged under ${DESTDIR}"

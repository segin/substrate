#!/bin/sh
# contrib/speex/build.sh — cross-build speex for substrate.
# Configured as a linux host (CC stays the substrate cross gcc) so the bundled
# libtool emits a shared library; the output is a substrate binary (OSABI 0x40).
# A codec dependency of PsyMP3.  libspeex itself does not link libogg (only the
# speexenc/speexdec example binaries do, and those are disabled), but we still
# point configure at the cross sysroot where libogg is staged.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; LIB="speex"; VERSION="1.2.1"
TREE="${HERE}/build/speex-${VERSION}"; BS="${HERE}/build/bs"
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
# substrate has an FPU, so keep the default float API.  SSE is off by default.
# -fno-stack-protector: substrate only defines __stack_chk_fail_local in crt0.
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr --enable-shared --enable-static \
  --disable-binaries \
  CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la
for so in "${DESTDIR}"/usr/lib/*.so.*; do [ -f "$so" ] && case "$so" in *.so.*.*) printf '\100' | dd of="$so" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null;; esac; done
# mirror to the cross sysroot so dependent ports (PsyMP3) find it
mkdir -p "${SR}/lib/pkgconfig" "${SR}/include/speex"
cp -a "${DESTDIR}"/usr/lib/libspeex.* "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/include/speex/. "${SR}/include/speex/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/speex.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true
echo "==> ${LIB} staged under ${DESTDIR}"

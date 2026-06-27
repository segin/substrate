#!/bin/sh
# contrib/libopus/build.sh — cross-build libopus for substrate.
# Configured as a linux host (CC stays the substrate cross gcc) so the bundled
# libtool emits a shared library; the output is a substrate binary (OSABI 0x40).
# libopus is standalone — the codec needs no libogg.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; LIB="libopus"; VERSION="1.5.2"
TREE="${HERE}/build/opus-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
rm -rf "${BS}"; mkdir -p "${BS}"; cd "${BS}"
# --disable-stack-protector: substrate's __stack_chk_fail_local lives only in
# crt0 (hidden, per-executable), so -fstack-protector in a shared lib leaves it
# undefined at link time.  Belt-and-suspenders with -fno-stack-protector in CFLAGS.
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr --enable-shared --enable-static \
  --disable-doc --disable-extra-programs --disable-stack-protector \
  CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la
for so in "${DESTDIR}"/usr/lib/*.so.*; do [ -f "$so" ] && case "$so" in *.so.*.*) printf '\100' | dd of="$so" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null;; esac; done
# mirror to the cross sysroot so dependent ports (PsyMP3) find it
mkdir -p "${SR}/lib/pkgconfig" "${SR}/include/opus"
cp -a "${DESTDIR}"/usr/lib/libopus.* "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/include/opus/. "${SR}/include/opus/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/opus.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true
echo "==> ${LIB} staged under ${DESTDIR}"

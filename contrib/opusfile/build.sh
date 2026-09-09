#!/bin/sh
# contrib/opusfile/build.sh — cross-build opusfile for substrate.
#
# opusfile is the high-level Ogg Opus decoder that sits on top of libogg +
# libopus.  sox's src/opus.c includes <opusfile.h> and nothing else provides
# it, so without this port sox configures with HAVE_OPUS false and ships
# with no Opus support at all -- silently, since a missing optional codec is
# not an error.
#
# Configured as a linux host (CC stays the substrate cross gcc) so the
# bundled libtool emits a shared library; the output is a substrate binary
# (OSABI 0x40).
#
# Depends on libogg and libopus, already staged in the cross sysroot.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; LIB="opusfile"; VERSION="0.12"
TREE="${HERE}/build/opusfile-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

[ -f "${SR}/lib/pkgconfig/ogg.pc" ]  || { echo "libogg not staged -- build contrib/libogg first" >&2; exit 1; }
[ -f "${SR}/lib/pkgconfig/opus.pc" ] || { echo "libopus not staged -- build contrib/libopus first" >&2; exit 1; }

# Let configure find libogg/libopus (staged in the cross sysroot) via
# pkg-config + flags.  The -I${SR}/include/opus is load-bearing and not
# redundant with -I${SR}/include: opusfile's own sources include
# <opus_multistream.h> by bare name, and opus.pc supplies that directory as
# "-I${includedir}/opus" which, with no PKG_CONFIG_SYSROOT_DIR, expands to
# the absolute /usr/include/opus -- the BUILD HOST's.  On a machine with
# libopus-dev installed that silently compiles against the host's headers;
# on a clean runner it is a hard "No such file".
export PKG_CONFIG_LIBDIR="${SR}/lib/pkgconfig"
export CPPFLAGS="-I${SR}/include -I${SR}/include/opus"
export LDFLAGS="-L${SR}/lib"

rm -rf "${BS}"; mkdir -p "${BS}"; cd "${BS}"
# --disable-http drops the openssl dependency; it only buys URL streaming,
# which sox does not use.  No --disable-stack-protector here (opusfile's
# configure has no such flag), so -fno-stack-protector goes in CFLAGS:
# substrate's __stack_chk_fail_local lives only in crt0 (hidden, per
# executable), so -fstack-protector in a shared lib leaves it undefined.
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr --enable-shared --enable-static \
  --disable-doc --disable-examples --disable-http \
  CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la
for so in "${DESTDIR}"/usr/lib/*.so.*; do [ -f "$so" ] && case "$so" in *.so.*.*) printf '\100' | dd of="$so" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null;; esac; done
# mirror to the cross sysroot so dependent ports (sox) find it
mkdir -p "${SR}/lib/pkgconfig" "${SR}/include/opus"
cp -a "${DESTDIR}"/usr/lib/libopusfile.*    "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/lib/libopusurl.*     "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/include/opus/.       "${SR}/include/opus/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/opusfile.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/opusurl.pc  "${SR}/lib/pkgconfig/" 2>/dev/null || true
echo "==> ${LIB} staged under ${DESTDIR}"

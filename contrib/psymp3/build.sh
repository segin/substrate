#!/bin/sh
#
# contrib/psymp3/build.sh — cross-build PsyMP3 (C++17 SDL2 music player) for substrate.
#
# PsyMP3 is autotools (configure.ac + generate-configure.sh/autogen.sh).  We
# regenerate configure on the HOST (needs autoconf/automake + autoconf-archive
# for AX_CXX_COMPILE_STDCXX_17), then configure as a *linux host* so the
# autotools/libtool machinery behaves — the compiler is still the substrate
# cross g++ (CC/CXX=), so the output is a real substrate ELF (OSABI 0x40).
#
# Disabled (and why):
#   --disable-mpris         : MPRIS needs D-Bus; substrate has no system bus.
#   --disable-rapidcheck    : property-test lib not ported.
#   --disable-test-harness  : test programs use SDL_main wrappers + extra libs.
#   --disable-final         : keep the normal multi-TU build (unity build is
#                             slower to debug and pulls all sources into one TU).
#
# Enabled codecs (all deps are staged in the cross sysroot):
#   FLAC (native, no libFLAC), Vorbis, Opus, Speex, AAC (faad2),
#   G.722 (spandsp), G.711 A-law/u-law, MP3 (bundled minimp3).
#
# Env: STAGE1_PREFIX (default /opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
PKG="psymp3"
TREE="${HERE}/build/psymp3"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${PKG}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

export PATH="${STAGE1_PREFIX}/bin:${PATH}"

[ -d "${TREE}/.git" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE}"

# --- 1. Regenerate configure on the host ----------------------------------
# autogen.sh -> generate-configure.sh runs `autoreconf -fiv`.  Requires
# autoconf-archive (provides AX_CXX_COMPILE_STDCXX_17) on the build host.
if [ ! -x ./configure ]; then
    ./autogen.sh
fi

# Swap in a substrate-aware config.sub/config.guess (binutils 2.46.0 ships the
# newest one).  Not strictly required here — we configure as a *linux* host and
# the autoreconf'd config.sub already recognises i386-unknown-linux-gnu — but we
# keep it for robustness in case a future host triple needs it.
CFGSUB="${SUBSTRATE_TOP}/contrib/binutils/build/binutils-2.46.0/config.sub"
CFGGUESS="${SUBSTRATE_TOP}/contrib/binutils/build/binutils-2.46.0/config.guess"
if ! ./config.sub i386-unknown-linux-gnu >/dev/null 2>&1; then
    [ -f "${CFGSUB}" ]   && cp "${CFGSUB}"   ./config.sub
    [ -f "${CFGGUESS}" ] && cp "${CFGGUESS}" ./config.guess
fi

# --- 2. Configure ----------------------------------------------------------
# pkg-config must resolve against the cross sysroot's .pc files.  The .pc files
# carry prefix=/usr, so they emit -I/usr/include/... and -l<name>; the cross
# g++'s default sysroot is ${SR}, so those resolve correctly on substrate.
export PKG_CONFIG_LIBDIR="${SR}/lib/pkgconfig"

./configure \
    --host=i386-unknown-linux-gnu \
    --prefix=/usr \
    CC=i386-unknown-substrate-gcc \
    CXX=i386-unknown-substrate-g++ \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector" \
    CXXFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector" \
    LDFLAGS="-L${SR}/lib -Wl,-rpath-link,${SR}/lib -Wl,--allow-shlib-undefined" \
    LIBS="-lpthread" \
    --disable-mpris --disable-rapidcheck --disable-test-harness --disable-final

# --- 3. Build --------------------------------------------------------------
make -j"${JOBS}"

# --- 4. Install + OSABI-stamp ---------------------------------------------
rm -rf "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# host/cross g++ stamps ELFOSABI_SYSV(0) on executables; substrate's loader
# routes on ELFOSABI_SUBSTRATE(0x40).  Stamp byte 7 of every produced ELF.
stamp_osabi() {
    [ -f "$1" ] || return 0
    printf '\100' | dd of="$1" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
}
stamp_osabi "${TREE}/src/psymp3"
stamp_osabi "${DESTDIR}/usr/bin/psymp3"

echo "==> ${PKG} staged under ${DESTDIR}"
i386-unknown-substrate-readelf -h "${DESTDIR}/usr/bin/psymp3" 2>/dev/null | grep -iE "OS/ABI|Type" || true

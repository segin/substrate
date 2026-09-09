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
#
# Assert it rather than trusting it.  An unexpanded m4 macro is not an
# autoreconf error: it passes through into the generated configure as a
# literal shell word, where it fails as "command not found" -- a message
# configure ignores.  The build then runs on for a hundred thousand lines
# and dies compiling C++17 sources, because without the macro AC_PROG_CXX's
# own probe leaves CXX at -std=gnu++11 and nothing raises it.  That is a
# multi-hour CI failure whose cause is one line near the very start, so
# check here where it is cheap and legible.
if [ ! -x ./configure ]; then
    aclocal --print-ac-dir >/dev/null 2>&1 && \
    ls "$(aclocal --print-ac-dir)"/ax_cxx_compile_stdcxx.m4 >/dev/null 2>&1 || {
        echo "psymp3: AX_CXX_COMPILE_STDCXX_17 is unavailable -- install autoconf-archive" >&2
        exit 1
    }
    ./autogen.sh
fi

# Swap in a substrate-aware config.sub/config.guess (binutils 2.46.0 ships the
# newest one).  Not strictly required here — we configure as a *linux* host and
# the autoreconf'd config.sub already recognises i386-unknown-linux-gnu — but we
# keep it for robustness in case a future host triple needs it.
# This configures as a *linux* host, so the tree's own config.sub already
# accepts the triple and this never fires -- but it named
# binutils-2.46.0 explicitly, which is both a version that will move and a
# tree that does not exist on a CI toolchain-cache hit.  The shared helper
# has neither problem.
if ! ./config.sub i386-unknown-linux-gnu >/dev/null 2>&1; then
    . "${HERE}/../substrate-autotools.sh"
    substrate_config_sub_fix "."
fi

# --- 2. Configure ----------------------------------------------------------
# pkg-config resolves against the cross sysroot's .pc files.  Those carry
# prefix=/usr and emit -l<name>, which is fine, and -I/usr/include/... which
# is NOT: an absolute -I is not rewritten by the compiler's sysroot, so it
# names the BUILD HOST's directory.  It goes unnoticed for headers that also
# sit at the top of ${SR}/include, because the default search path finds
# them anyway -- but psymp3 includes <SDL.h> and <ft2build.h>, which live in
# subdirectories, and sdl2.pc/freetype2.pc supply those subdirectories
# through exactly such a -I.  On a box with libsdl2-dev installed it
# compiles against the host's headers; on a clean runner it stops at
#
#     include/psymp3.h:267:10: fatal error: SDL.h: No such file or directory
#
# Name them explicitly against ${SR}.  SDL2, not SDL: the sysroot has both,
# SDL/ being sdl12-compat's, and configure.ac asks for sdl2.
export PKG_CONFIG_LIBDIR="${SR}/lib/pkgconfig"
SUBDIR_INCS="-I${SR}/include/SDL2 -I${SR}/include/freetype2"

./configure \
    --host=i386-unknown-linux-gnu \
    --prefix=/usr \
    CC=i386-unknown-substrate-gcc \
    CXX=i386-unknown-substrate-g++ \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fPIE ${SUBDIR_INCS}" \
    CXXFLAGS="-march=i486 -mtune=i486 -O2 -g -fPIE ${SUBDIR_INCS}" \
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

#!/bin/sh
#
# contrib/sdl2/build.sh — cross-build SDL 2.x for substrate.
#
# Video:   X11 (substrate ships the Xlib client stack) + the dummy driver.
#          wayland/kmsdrm/vulkan/opengl/opengles are off (no driver on target).
# Audio:   dummy + disk for now (substrate's /dev/audio is Sun-SADA, not OSS;
#          a real backend is follow-up work).
# Threads: pthreads (libpthread).  loadso: dlopen (libdl).
#
# Env: STAGE1_PREFIX (default /opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="sdl2"
VERSION="2.30.9"
TREE_DIR="${HERE}/build/SDL2-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Pull in every staged X dist tree plus libiconv (SDL uses iconv for text).
PKGP=""; CPP=""; LDF=""
for d in xorgproto xcb-proto libXau xtrans libxcb libX11 \
         libXext libICE libSM libXt libXmu libXpm libXaw \
         libXScrnSaver libiconv; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"

export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries -Wl,--allow-shlib-undefined"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> configure"
# Configure as a linux host (substrate is linux-like: ELF, pthreads, dlopen,
# X11) so SDL's bundled libtool emits a proper shared library — it has no
# 'substrate' host case and would otherwise build static-only.  The compiler is
# still the substrate cross gcc (CC=), so the output is a substrate binary; the
# linux-specific subsystems that don't apply are disabled below + post-configure.
"${TREE_DIR}/configure" \
    --host=i386-unknown-linux-gnu \
    --prefix=/usr \
    --enable-shared --enable-static \
    --enable-video-x11 --enable-video-dummy \
    --disable-video-wayland --disable-video-kmsdrm --disable-video-vulkan \
    --disable-video-opengl --disable-video-opengles \
    --disable-video-vivante --disable-video-cocoa --disable-video-directfb \
    --disable-wayland-shared --disable-x11-shared \
    --enable-pthreads --enable-threads \
    --disable-alsa --disable-sndio --disable-pulseaudio --disable-jack \
    --disable-esd --disable-arts --disable-nas --disable-oss \
    --disable-libudev --disable-dbus --disable-ibus --disable-fcitx \
    --disable-joystick --disable-haptic --disable-sensor --disable-power \
    --disable-joystick-virtual --disable-hidapi \
    --disable-rpath \
    CC=i386-unknown-substrate-gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    ${CONFIGURE_EXTRA:-}

# substrate uses X11 for input; its linux/input.h + console headers are partial,
# so turn off the linux evdev/keyboard input core (these guard on #ifdef, so the
# defines must be REMOVED, not set to 0).
sed -i \
    -e 's|#define HAVE_LINUX_INPUT_H 1|/* HAVE_LINUX_INPUT_H off (substrate uses X11 input) */|' \
    -e 's|#define SDL_INPUT_LINUXEV 1|/* SDL_INPUT_LINUXEV off (substrate uses X11 input) */|' \
    -e 's|#define SDL_INPUT_LINUXKD 1|/* SDL_INPUT_LINUXKD off (substrate uses X11 input) */|' \
    include/SDL_config.h

echo "==> make"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

rm -f "${DESTDIR}"/usr/lib/*.la

# host cc -shared stamps ELFOSABI_SYSV(0); cross-ld needs SUBSTRATE(0x40).
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    case "${so}" in *.so.*.*) printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null ;; esac
done

echo "==> Done.  ${LIB} staged under ${DESTDIR}"

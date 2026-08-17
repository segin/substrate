#!/bin/sh
#
# contrib/sdl3/build.sh — cross-build SDL3 for substrate.
#
# SDL3 dropped autotools, so this is a CMake build driven by the reusable
# toolchain at contrib/cmake/substrate.toolchain.cmake.  That file names the
# platform "Substrate" (not Linux), which is what the sdlplatform.cmake patch
# keys on to select substrate's audio backend.
#
# Video:   X11 (substrate ships the Xlib client stack) + the dummy driver.
#          wayland/kmsdrm/vulkan/opengl/opengles are off — no driver on target.
# Audio:   the NetBSD audio(4) backend on substrate's Sun/SADA /dev/audio.
#          substrate's <sys/audioio.h> matches NetBSD 10's audio_info, so the
#          netbsd backend compiles and runs as-is; the CMake patch routes
#          SUBSTRATE there and the SDL_audiodev.c patch points the default
#          device at /dev/audio instead of the OSS /dev/dsp.
#          alsa/pulse/pipewire/jack/sndio/oss are all off — none exist here.
# Threads: pthreads (libpthread).  loadso: dlopen (libdl).
# Input:   X11 only.  substrate ships a partial <linux/input.h> (enough for
#          EVIOCGNAME, not for the full evdev/force-feedback ABI), so the
#          probe would pass and then the evdev core would fail to compile.
#          HAVE_LINUX_INPUT_H is pre-seeded false to keep it out — the same
#          thing the SDL2 port achieves by stripping SDL_INPUT_LINUXEV et al
#          from the generated config.  Joystick/haptic are off outright: no
#          backend exists on target.
#
# Env: STAGE1_PREFIX (default /opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="sdl3"
VERSION="3.4.14"
TREE_DIR="${HERE}/build/SDL3-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"
X11ROOT="${HERE}/build/x11root"

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
: "${SUBSTRATE_SYSROOT:=${STAGE1_PREFIX}/i386-unknown-substrate}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# --- mini-sysroot ----------------------------------------------------------
# Merge the staged X client trees plus libiconv into one prefix so CMake's
# FindX11 and the pkg-config probes see a single -I/-L pair.  libXrandr is not
# ported; SDL3's X11 driver treats it as optional (HAVE_XRANDR_H gates it).
rm -rf "${X11ROOT}"; mkdir -p "${X11ROOT}/usr"
_have=0
for d in xorgproto xtrans libXau libxcb libX11 libXext \
         libXcursor libXi libXfixes libXrender libXScrnSaver libiconv; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    cp -a "${st}/usr/." "${X11ROOT}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 12 ] || {
    echo "build.sh: only ${_have}/12 X dist trees found — build the X chain first" >&2
    exit 1
}

PKGP="${X11ROOT}/usr/lib/pkgconfig:${X11ROOT}/usr/share/pkgconfig"
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"
export PKG_CONFIG_LIBDIR="${PKGP}"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> cmake configure"
cmake "${TREE_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${SUBSTRATE_TOP}/contrib/cmake/substrate.toolchain.cmake" \
    -DCMAKE_FIND_ROOT_PATH="${SUBSTRATE_SYSROOT};${X11ROOT}/usr" \
    -DCMAKE_PREFIX_PATH="${X11ROOT}/usr" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-march=i486 -mtune=i486 -I${X11ROOT}/usr/include" \
    -DCMAKE_EXE_LINKER_FLAGS="-L${X11ROOT}/usr/lib -Wl,-rpath-link,${X11ROOT}/usr/lib" \
    -DCMAKE_SHARED_LINKER_FLAGS="-L${X11ROOT}/usr/lib -Wl,-rpath-link,${X11ROOT}/usr/lib -l:libc.so.0 -l:libsys.so.0" \
    -DSDL_SHARED=ON -DSDL_STATIC=ON \
    -DSDL_X11=ON -DSDL_DUMMYVIDEO=ON \
    -DSDL_X11_XRANDR=OFF \
    -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_VULKAN=OFF \
    -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VIVANTE=OFF -DSDL_RPI=OFF \
    -DSDL_OSS=OFF -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF \
    -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
    -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_LIBURING=OFF \
    -DSDL_HIDAPI=OFF -DSDL_LIBC=ON \
    -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF \
    -DHAVE_LINUX_INPUT_H=0 \
    -DSDL_RPATH=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    ${CONFIGURE_EXTRA:-}

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# The cross ld stamps ELFOSABI_SYSV(0) on its output; substrate's ld.so routes
# shared-object personality dispatch on the OSABI byte, so brand the staged
# tree SUBSTRATE(0x40).  Same one-byte post-step every other port applies.
sh "${SUBSTRATE_TOP}/contrib/cmake/substrate-osabi-stamp.sh" "${DESTDIR}"

echo "==> Done.  ${LIB} staged under ${DESTDIR}"

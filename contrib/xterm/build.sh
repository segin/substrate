#!/bin/sh
#
# contrib/xterm/build.sh — cross-build xterm for substrate.
# xterm is the classic X11 terminal emulator.  This build uses the
# core X bitmap fonts and the Athena-widget toolbar; TrueType /
# Xft / fontconfig is disabled (that font stack is not ported).
#
# Depends on the whole X library chain (contrib/{xorgproto,libxcb,
# libXau,xtrans,libX11,libXext,libICE,libSM,libXt,libXmu,libXpm,
# libXaw}) and ncurses, all staged first.
#
# xterm's AC_PATH_X wants a single X include dir and a single X lib
# dir, so the staged dist trees are merged into one mini-sysroot
# (build/x11root) and pointed at via --x-includes / --x-libraries.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-xterm)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="410"
TREE_DIR="${HERE}/build/xterm-${VERSION}"
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
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-xterm}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Merge every staged X dist tree + ncurses into one mini-sysroot.
rm -rf "${X11ROOT}"
mkdir -p "${X11ROOT}/usr"
_have=0
for d in xorgproto xcb-proto libXau xtrans libxcb libX11 \
         libXext libICE libSM libXt libXmu libXpm libXaw ncurses; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    cp -a "${st}/usr/." "${X11ROOT}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 13 ] || {
    echo "build.sh: only ${_have} of 14 X/ncurses dist trees found —" >&2
    echo "         build the X library chain + ncurses first" >&2
    exit 1
}

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# substrate's setpgrp(2) is the POSIX no-argument form; xterm's
# AC_FUNC_SETPGRP cannot run its probe when cross-compiling.
export ac_cv_func_setpgrp_void=yes

export PKG_CONFIG_LIBDIR="${X11ROOT}/usr/lib/pkgconfig:${X11ROOT}/usr/share/pkgconfig:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"
# -DHAVE_GRANTPT_PTY_ISATTY: substrate has Unix98 ptys (/dev/ptmx,
# /dev/pts/N, posix_openpt/grantpt/unlockpt/ptsname in libc).  xterm's
# get_pty() uses that path only when HAVE_GRANTPT_PTY_ISATTY is set,
# but configure can only set it by *running* a probe — impossible when
# cross-compiling, so it falls through to BSD-style pty_search() over
# /dev/pty?? nodes that don't exist ("get_pty: not enough ptys").
# Forcing the macro on routes get_pty() through posix_openpt()+ptsname().
export CPPFLAGS="-I${X11ROOT}/usr/include -DHAVE_GRANTPT_PTY_ISATTY"
# Build xterm as a PIE.  A non-PIE executable reaches shared-library
# data (the libXt/libXaw WidgetClass globals) through R_386_COPY
# relocations; a PIE uses R_386_GLOB_DAT instead — the path every
# substrate bin/ program already uses.  COPY left xterm's
# sessionShellWidgetClass NULL ("XtAppCreateShell requires non-NULL
# widget class").
export LDFLAGS="-L${X11ROOT}/usr/lib -Wl,-rpath-link,${X11ROOT}/usr/lib -Wl,--copy-dt-needed-entries -pie"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --with-x \
    --x-includes="${X11ROOT}/usr/include" \
    --x-libraries="${X11ROOT}/usr/lib" \
    --with-app-defaults=/usr/share/X11/app-defaults \
    --disable-freetype \
    --disable-imake \
    --disable-luit \
    --enable-toolbar \
    --enable-256-color \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fPIE"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  xterm staged under ${DESTDIR}"

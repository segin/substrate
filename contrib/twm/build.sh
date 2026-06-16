#!/bin/sh
# contrib/twm/build.sh — cross-build twm for substrate.
#
# twm is the classic minimal X11 window manager.  It depends on the core X
# client library chain (xorgproto, libX11, libXext, libXt, libXmu, libICE,
# libSM) and uses a yacc/lex parser for its config grammar — yacc/lex run on
# the BUILD host and emit C that the cross compiler builds.
#
# Like the xterm port, the staged X dist trees are merged into one
# mini-sysroot (build/x11root) and exposed via pkg-config.  twm links as a
# PIE so it reaches libXt/libXmu WidgetClass globals through R_386_GLOB_DAT
# rather than R_386_COPY (the same path every substrate bin/ program uses).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.0.12"
TREE_DIR="${HERE}/build/twm-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"
X11ROOT="${HERE}/build/x11root"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-twm}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Merge the staged X dist trees into one mini-sysroot.
rm -rf "${X11ROOT}"; mkdir -p "${X11ROOT}/usr"
_have=0
for d in xorgproto libXau xtrans libxcb libX11 libXext libICE libSM libXt libXmu; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    cp -a "${st}/usr/." "${X11ROOT}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 9 ] || { echo "build.sh: only ${_have}/10 X dist trees found — build the X chain first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

export PKG_CONFIG_LIBDIR="${X11ROOT}/usr/lib/pkgconfig:${X11ROOT}/usr/share/pkgconfig:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"
export CPPFLAGS="-I${X11ROOT}/usr/include"
export LDFLAGS="-L${X11ROOT}/usr/lib -Wl,-rpath-link,${X11ROOT}/usr/lib -Wl,--copy-dt-needed-entries -pie"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --with-appdefaultdir=/usr/share/X11/app-defaults \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    YACC=yacc \
    LEX=flex \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fPIE"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
echo "==> Done.  twm staged at ${DESTDIR}/usr/bin/twm"

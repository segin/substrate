#!/bin/sh
#
# contrib/libXScrnSaver/build.sh — cross-build libXScrnSaver (libXss) for
# substrate.  The X Screen Saver client extension library
# (XScreenSaverQueryInfo/Register/SelectInput/...).  Mirrors
# contrib/libXinerama/build.sh.  Dependency of CDE's dtsession.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-libXScrnSaver)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="libXScrnSaver"
VERSION="1.2.4"
TREE_DIR="${HERE}/build/${LIB}-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-${LIB}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Teach the bundled config.sub about the substrate OS (idempotent).
if ! grep -q 'substrate\*' "${TREE_DIR}/config.sub"; then
    sed -i 's/\(| sortix\* \)/\1| substrate* /' "${TREE_DIR}/config.sub"
fi
# Teach libtool's generated configure to treat substrate* like linux* for
# shared libraries (otherwise --enable-shared still emits only .a files).
if ! grep -q 'substrate\*' "${TREE_DIR}/configure"; then
    sed -i \
      -e 's@linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*)@linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*)@g' \
      -e 's@gnu\* | linux\* | tpf\* | k\*bsd\*-gnu | kopensolaris\*-gnu)@gnu* | linux* | tpf* | k*bsd*-gnu | kopensolaris*-gnu | substrate*)@g' \
      -e 's@^\(\s*\)linux\*)@\1linux* | substrate*)@g' \
      "${TREE_DIR}/configure"
fi

PKGP=""; CPP=""; LDF=""
for d in xorgproto xcb-proto libXau xtrans libxcb libX11 libXext; do
    st="${SUBSTRATE_TOP}/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"

export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --includedir=/usr/include \
    --enable-shared \
    --enable-static \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    xorg_cv_malloc0_returns_null=no \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la

_n=0
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    _n=$((_n + 1))
done
echo "  OSABI->substrate on ${_n} shared objects"
echo "==> Done.  ${LIB} staged under ${DESTDIR}"

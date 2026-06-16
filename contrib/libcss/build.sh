#!/bin/sh
#
# contrib/libcss/build.sh — cross-compile the NetSurf HTML5
# parser for substrate.  Static libcss.a + headers + .pc.
#
# Depends on libparserutils + libwapcaplet already being staged in
# the cross-toolchain sysroot (their pkg-config .pc files, with the
# prefix rewritten to the sysroot path, must be reachable).
#
# Env: STAGE1_PREFIX (/opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="libcss"
VERSION="0.9.2"
BS_VERSION="1.10"
TREE_DIR="${HERE}/build/${LIB}-${VERSION}"
BS_DIR="${HERE}/build/buildsystem-${BS_VERSION}"

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
SYSROOT="${STAGE1_PREFIX}/i386-unknown-substrate"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -f "${SYSROOT}/lib/pkgconfig/libparserutils.pc" ] || {
    echo "build.sh: libparserutils not staged in the sysroot" >&2; exit 1; }

cd "${TREE_DIR}"
export CFLAGS="-Wno-error -march=i586 -mtune=i686 -O2 -fno-pie"
# pkg-config resolves libparserutils / libwapcaplet from the sysroot.
export PKG_CONFIG_LIBDIR="${SYSROOT}/lib/pkgconfig"

NSMAKE="make NSSHARED=${BS_DIR} CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar BUILD_CC=cc PKGCONFIG=pkg-config \
    COMPONENT_TYPE=lib-static PREFIX=/usr"

echo "==> make"
${NSMAKE} -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
${NSMAKE} DESTDIR="${DESTDIR}" install

echo "==> Done.  ${LIB}.a staged under ${DESTDIR}"

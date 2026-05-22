#!/bin/sh
#
# contrib/xtrans/build.sh — install the X transport-layer sources
# for substrate.  Produces (header-only, no compiled objects):
#   /usr/include/X11/Xtrans/*.{h,c}    transport implementation
#   /usr/share/aclocal/xtrans.m4       autoconf macro
#   /usr/lib/pkgconfig/xtrans.pc       metadata
#
# xtrans is the X11 network-transport layer.  It is distributed as
# header + .c files that the *consumer* (#include)s and compiles
# itself — libX11 builds Xtranssock.c / Xtranslcl.c straight into
# libX11.  Substrate's AF_UNIX + TCP socket layer is the runtime
# backing; the OS-specific transport selection happens in libX11's
# configure, so nothing here needs a substrate transport patch.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-xtrans)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.6.0"
TREE_DIR="${HERE}/build/xtrans-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-xtrans}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --includedir=/usr/include \
    --disable-docs \
    CC=i386-unknown-substrate-gcc

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# xtrans installs xtrans.pc to $(datadir)/pkgconfig
# (/usr/share/pkgconfig).  Relocate it into /usr/lib/pkgconfig so
# the repo-root build.sh sync_to_sysroot step carries it into the
# cross sysroot for libX11.
if [ -d "${DESTDIR}/usr/share/pkgconfig" ]; then
    mkdir -p "${DESTDIR}/usr/lib/pkgconfig"
    mv "${DESTDIR}"/usr/share/pkgconfig/*.pc "${DESTDIR}/usr/lib/pkgconfig/" 2>/dev/null || true
    rmdir "${DESTDIR}/usr/share/pkgconfig" 2>/dev/null || true
fi

echo "==> Done.  xtrans staged under ${DESTDIR}"

#!/bin/sh
#
# contrib/xcb-proto/build.sh — install the XCB protocol
# descriptions for substrate.  Produces:
#   /usr/share/xcb/*.xml                  X protocol descriptions
#   /usr/lib/python*/site-packages/xcbgen Python code generator
#   /usr/lib/pkgconfig/xcb-proto.pc       metadata
#
# xcb-proto is a BUILD-TIME dependency of libxcb: libxcb's build
# imports the xcbgen Python package to turn the .xml descriptions
# into C source.  Nothing here is compiled — it is data + Python.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-xcb-proto)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.17.0"
TREE_DIR="${HERE}/build/xcb-proto-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-xcb-proto}"
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
    --libdir=/usr/lib

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# xcb-proto installs xcb-proto.pc to $(datarootdir)/pkgconfig
# (/usr/share/pkgconfig).  Relocate it into /usr/lib/pkgconfig so
# the repo-root build.sh sync_to_sysroot step carries it into the
# cross sysroot for libxcb.
if [ -d "${DESTDIR}/usr/share/pkgconfig" ]; then
    mkdir -p "${DESTDIR}/usr/lib/pkgconfig"
    mv "${DESTDIR}"/usr/share/pkgconfig/*.pc "${DESTDIR}/usr/lib/pkgconfig/" 2>/dev/null || true
    rmdir "${DESTDIR}/usr/share/pkgconfig" 2>/dev/null || true
fi

echo "==> Done.  xcb-proto staged under ${DESTDIR}"

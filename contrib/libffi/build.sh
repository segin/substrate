#!/bin/sh
# contrib/libffi/build.sh — cross-build libffi for substrate (i386).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="3.4.6"
TREE_DIR="${HERE}/build/libffi-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-libffi}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
# Teach libffi's libtool that substrate builds ELF shared libraries; without this
# --enable-shared yields only libffi.a (libtool's host_os case has no substrate
# branch -> build_libtool_libs=no), and gobject/gio can't resolve ffi_* at load.
sh "${HERE}/../substrate-libtool-shared.sh" "${TREE_DIR}/configure"
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --enable-shared --enable-static \
    --disable-docs \
    --disable-multi-os-directory \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"
echo "==> make -j${JOBS}"
make -j"${JOBS}"
echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
echo "==> Done.  Staged at ${DESTDIR}/usr/{lib/libffi.*,include/ffi.h,include/ffitarget.h}"

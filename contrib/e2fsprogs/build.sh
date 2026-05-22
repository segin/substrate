#!/bin/sh
#
# contrib/e2fsprogs/build.sh — cross-build e2fsprogs for substrate.
# Produces the ext2/3/4 filesystem toolset and its libraries:
#   /usr/sbin/{mke2fs,e2fsck,tune2fs,dumpe2fs,debugfs,resize2fs,...}
#   /usr/lib/{libext2fs,libcom_err,libe2p,libss,libuuid,libblkid}.a
#   /usr/include/{ext2fs,et,e2p,ss,uuid,blkid}/*.h
#   /usr/lib/pkgconfig/{ext2fs,com_err,e2p,ss,uuid,blkid}.pc
#
# Static libraries only — e2fsprogs builds shared objects through
# its own hand-rolled ELF rules which have no substrate host_os
# case; the static archives are what contrib/e2tools links.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-e2fsprogs)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.47.2"
TREE_DIR="${HERE}/build/e2fsprogs-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-e2fsprogs}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# badblocks.c's progress display refines its backspace width with
# mbstowcs()+wcswidth() under #ifdef HAVE_MBSTOWCS.  Substrate's
# libc has mbstowcs but not wcswidth.  Gating HAVE_MBSTOWCS off
# drops only that refinement — the byte length from snprintf() is
# used instead, which is correct for substrate's C locale.
export ac_cv_func_mbstowcs=no

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --with-root-prefix=/usr \
    --disable-nls \
    --disable-fuse2fs \
    --disable-uuidd \
    --disable-elf-shlibs \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    BUILD_CC=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -include sys/time.h" \
    LDFLAGS="-fno-pie -Wl,--copy-dt-needed-entries"

# RDYNAMIC= : configure unconditionally sets RDYNAMIC=-rdynamic for
# GCC, but the substrate cross-gcc's link spec does not implement
# -rdynamic.  It only controls dynamic-symbol export of the e2fsck
# binary (not a plugin host), so dropping it is harmless.
echo "==> make -j${JOBS}"
make -j"${JOBS}" RDYNAMIC=

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}" RDYNAMIC=

echo "==> Done.  e2fsprogs staged under ${DESTDIR}"

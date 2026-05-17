#!/bin/sh
#
# build.sh — cross-build bzip2 for substrate.
#
# bzip2 has a hand-written Makefile (no autoconf) so we just point
# its CC/AR/RANLIB at substrate's stage-1 cross toolchain.  We build
# the static library (libbz2.a) and CLI binaries via the main
# Makefile, then shell out to Makefile-libbz2_so for the shared
# libbz2.so.1.0.8 — same split bzip2 expects.  Skips bzip2's
# self-test target (runs the host binary against canned input,
# which we can't do for a cross-build).
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-bzip2)
#   JOBS            parallel jobs (default `nproc`)
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.0.8"
TREE_DIR="${HERE}/build/bzip2-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-bzip2}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

CROSS_CC=i386-unknown-substrate-gcc
CROSS_AR=i386-unknown-substrate-ar
CROSS_RANLIB=i386-unknown-substrate-ranlib

command -v "${CROSS_CC}" >/dev/null 2>&1 || {
    echo "build.sh: ${CROSS_CC} not on PATH (PATH=${PATH})" >&2
    exit 1
}

cd "${TREE_DIR}"

# Static library + CLI tools.  -fno-pie because the substrate base
# image links userland with -no-pie; libsys/libc DT_NEEDED-flow is
# the linker's job, not ours.
echo "==> Building static libbz2.a + bzip2 + bzip2recover"
make clean >/dev/null 2>&1 || true
make -j"${JOBS}" \
    CC="${CROSS_CC}" \
    AR="${CROSS_AR}" \
    RANLIB="${CROSS_RANLIB}" \
    CFLAGS="-Wall -Winline -O2 -g -D_FILE_OFFSET_BITS=64 -fno-pie" \
    LDFLAGS="-fno-pie" \
    libbz2.a bzip2 bzip2recover

# Shared library via the auxiliary Makefile.
echo "==> Building shared libbz2.so.1.0.8"
make -f Makefile-libbz2_so clean >/dev/null 2>&1 || true
make -j"${JOBS}" -f Makefile-libbz2_so \
    CC="${CROSS_CC}" \
    CFLAGS="-fPIC -Wall -Winline -O2 -g -D_FILE_OFFSET_BITS=64"

# Stage into DESTDIR with substrate's `/usr/{bin,lib,include}` layout.
echo "==> Installing into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}/usr/bin" "${DESTDIR}/usr/lib" "${DESTDIR}/usr/include"

cp -a bzip2          "${DESTDIR}/usr/bin/bzip2"
cp -a bzip2recover   "${DESTDIR}/usr/bin/bzip2recover"
# Standard symlinks expected by tar/script callers.
ln -sf bzip2         "${DESTDIR}/usr/bin/bunzip2"
ln -sf bzip2         "${DESTDIR}/usr/bin/bzcat"

cp -a libbz2.a              "${DESTDIR}/usr/lib/libbz2.a"
cp -a libbz2.so.1.0.8       "${DESTDIR}/usr/lib/libbz2.so.1.0.8"
ln -sf libbz2.so.1.0.8      "${DESTDIR}/usr/lib/libbz2.so.1.0"
ln -sf libbz2.so.1.0        "${DESTDIR}/usr/lib/libbz2.so.1"
ln -sf libbz2.so.1          "${DESTDIR}/usr/lib/libbz2.so"

cp -a bzlib.h               "${DESTDIR}/usr/include/bzlib.h"

# substrate's cross-ld stamps ELFOSABI_SYSV (0) on shared libs;
# patch the OSABI byte in libbz2.so.1.0.8 to ELFOSABI_SUBSTRATE (64)
# the same way lib/c does.  Without this the substrate kernel's
# exec-personality dispatch routes the DSO down the wrong loader.
printf '\x40' | dd of="${DESTDIR}/usr/lib/libbz2.so.1.0.8" \
                   bs=1 seek=7 count=1 conv=notrunc status=none

echo "==> Done.  Files staged in ${DESTDIR}/usr/{bin,lib,include}"

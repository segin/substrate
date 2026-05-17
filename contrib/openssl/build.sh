#!/bin/sh
#
# build.sh — Configure + build + install OpenSSL for substrate.
#
# Uses the linux-generic32 Configure target with no-asm so we don't
# pull in i386 ASM bits that may not assemble against substrate-
# binutils.  Disables tests/engine/quic to keep the build small.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-openssl)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="3.0.13"
TREE_DIR="${HERE}/build/openssl-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-openssl}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

# OpenSSL builds .so files with -Wl,-z,defs (no undefined symbols
# allowed at link time).  Substrate-gcc's default for `-shared` does
# NOT auto-add -lc, so symbols like memset go unresolved.  Force the
# default-libs onto every link line.
export LDFLAGS="${LDFLAGS:-} -Wl,--no-as-needed -lc"

echo "==> Configure"
./Configure linux-generic32 \
    --prefix=/usr \
    --openssldir=/etc/ssl \
    --cross-compile-prefix=i386-unknown-substrate- \
    no-asm \
    no-engine \
    no-tests \
    no-threads \
    shared

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install_sw install_ssldirs DESTDIR="${DESTDIR}"

echo "==> Done.  Staged libs and headers under ${DESTDIR}/usr"

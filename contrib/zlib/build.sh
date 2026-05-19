#!/bin/sh
#
# contrib/zlib/build.sh — configure + build + install zlib for
# substrate.  Produces:
#   /usr/lib/libz.a + /usr/lib/libz.so.1 + /usr/lib/libz.so
#   /usr/include/zlib.h, zconf.h
#
# zlib uses a hand-rolled shell configure (not autoconf).  We
# drive it via env vars and skip its host-detection probes.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.3.1"
TREE_DIR="${HERE}/build/zlib-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-zlib}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"
# zlib's configure honors these env vars.
export CC=i386-unknown-substrate-gcc
export AR=i386-unknown-substrate-ar
export RANLIB=i386-unknown-substrate-ranlib
export CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"
export LDFLAGS="-fno-pie -Wl,--copy-dt-needed-entries"

echo "==> configure"
./configure --prefix=/usr --libdir=/usr/lib --includedir=/usr/include

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  zlib staged under ${DESTDIR}"

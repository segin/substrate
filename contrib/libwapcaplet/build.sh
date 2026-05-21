#!/bin/sh
#
# contrib/libwapcaplet/build.sh — cross-compile the NetSurf
# string-interning library for substrate.  Static libwapcaplet.a
# + headers + pkg-config file.  See contrib/libparserutils for
# notes on the NetSurf buildsystem cross-compile mechanism.
#
# Env: STAGE1_PREFIX (/opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="libwapcaplet"
VERSION="0.4.3"
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
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-${LIB}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"
export CFLAGS="-Wno-error -march=i586 -mtune=i686 -O2 -fno-pie"

NSMAKE="make NSSHARED=${BS_DIR} CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar BUILD_CC=cc \
    COMPONENT_TYPE=lib-static PREFIX=/usr"

echo "==> make"
${NSMAKE} -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
${NSMAKE} DESTDIR="${DESTDIR}" install

echo "==> Done.  ${LIB}.a staged under ${DESTDIR}"

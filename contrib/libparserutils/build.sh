#!/bin/sh
#
# contrib/libparserutils/build.sh — cross-compile the NetSurf
# parser-utils library for substrate.  Produces a static
# libparserutils.a + headers + pkg-config file.
#
# NetSurf components build with the shared "buildsystem" makefiles:
# the cross compiler is taken from CC, and HOST is auto-derived from
# `CC -dumpmachine`, so BUILD != HOST puts the buildsystem into
# cross mode automatically.
#
# Env:
#   STAGE1_PREFIX   default /opt/substrate
#   DESTDIR         default ${SUBSTRATE_TOP}/dist-overlay/dist-libparserutils
#   JOBS            default `nproc`

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="libparserutils"
VERSION="0.2.5"
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

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

# The buildsystem appends $(CFLAGS) after its own -Werror WARNFLAGS,
# so -Wno-error here neutralises -Werror under modern GCC.
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

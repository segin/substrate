#!/bin/sh
#
# build.sh — configure + build + install GNU Automake for substrate.
#
# Automake is perl scripts and data files; nothing is compiled for the
# target.  It is built on the host with the perl path it will use on
# substrate (/usr/bin/perl, contrib/perl).  configure checks the host's
# autoconf, which must be >= 2.65.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.18.1"
TREE_DIR="${HERE}/build/automake-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-automake}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
echo "==> configure"
"${TREE_DIR}/configure" \
    --prefix=/usr \
    PERL=/usr/bin/perl

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  Staged at ${DESTDIR}/usr/bin/automake"

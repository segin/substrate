#!/bin/sh
#
# build.sh — configure + build + install GNU sed for substrate.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="4.9"
TREE_DIR="${HERE}/build/sed-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-sed}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --disable-nls \
    --disable-acl \
    --disable-threads \
    CFLAGS="-O2 -g -march=i486 -mtune=i486"

echo "==> make -j${JOBS}"
# Skip gnulib-tests; they don't run under a cross build anyway and a
# couple need fpurge/freading substrate ports we haven't written yet.
make -j"${JOBS}" SUBDIRS="po ."

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}" SUBDIRS="po ."

echo "==> Done.  Staged at ${DESTDIR}/usr/bin/sed"

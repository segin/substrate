#!/bin/sh
#
# build.sh — compile + install OpenBSD expr for substrate.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-expr}"
: "${CROSS:=${STAGE1_PREFIX}/bin/i386-unknown-substrate-}"

[ -f "${BUILD_DIR}/expr.c" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

echo "==> Compiling expr"
"${CROSS}gcc" \
    -O2 -g -march=i486 -mtune=i486 \
    -Wall \
    -o "${BUILD_DIR}/expr" \
    "${BUILD_DIR}/expr.c" \
    -lregex

echo "==> Installing into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}/usr/bin" "${DESTDIR}/usr/share/man/man1"
cp "${BUILD_DIR}/expr" "${DESTDIR}/usr/bin/expr"
chmod 0755 "${DESTDIR}/usr/bin/expr"
cp "${BUILD_DIR}/expr.1" "${DESTDIR}/usr/share/man/man1/expr.1"

echo "==> Done.  Staged at ${DESTDIR}/usr/bin/expr"

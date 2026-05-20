#!/bin/sh
#
# contrib/quickjs/build.sh — cross-compile Fabrice Bellard's QuickJS
# for substrate.  Produces:
#   /usr/bin/{qjs,qjsc}            interpreter + bytecode compiler
#   /usr/lib/quickjs/libquickjs.a  static engine library
#   /usr/include/quickjs/{quickjs.h,quickjs-libc.h}
#
# QuickJS's Makefile already supports cross builds via CROSS_PREFIX:
# it builds a host-side qjsc ("host-qjsc") with the host gcc to turn
# repl.js into C, and cross-compiles everything else.  The substrate
# patch series adds a TARGET_CFLAGS / TARGET_LDFLAGS hook so the
# cross objects can carry -march=i486 / -fno-pie without polluting
# the host build.
#
# Env:
#   STAGE1_PREFIX   default /opt/substrate
#   DESTDIR         default ${SUBSTRATE_TOP}/dist-quickjs
#   JOBS            default `nproc`

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2025-09-13"
TREE_DIR="${HERE}/build/quickjs-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-quickjs}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"
make clean >/dev/null 2>&1 || true

echo "==> make -j${JOBS} (cross)"
# LDEXPORT defaults to -rdynamic, which the substrate cross-gcc does
# not accept; clear it.  -rdynamic only matters for native (.so)
# QuickJS modules dlopen'd back into qjs, which substrate does not
# build — the interpreter and libquickjs.a are unaffected.
make -j"${JOBS}" \
    CROSS_PREFIX=i386-unknown-substrate- \
    PREFIX=/usr \
    LDEXPORT= \
    TARGET_CFLAGS="-march=i586 -mtune=i686 -O2 -fno-pie -DSUBSTRATE" \
    TARGET_LDFLAGS="-fno-pie -Wl,--copy-dt-needed-entries"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install PREFIX=/usr DESTDIR="${DESTDIR}" CROSS_PREFIX=i386-unknown-substrate-

echo "==> Done.  /usr/bin/{qjs,qjsc} + libquickjs.a staged under ${DESTDIR}"

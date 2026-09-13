#!/bin/sh
#
# build.sh — configure + build + install ncurses for substrate.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="6.4"
TREE_DIR="${HERE}/build/ncurses-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-ncurses}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

# cf_cv_header_stdbool_h=1: configure's "should we include stdbool.h" probe
# answers no here, which bakes NCURSES_ENABLE_STDBOOL_H 0 into curses.h.  That
# makes the header do
#
#     #undef bool
#     #define bool NCURSES_BOOL          /* typedef unsigned char */
#
# -- redefining bool out from under any program that included <stdbool.h>
# first.  nano does, and dies on its own `bool` flag being assigned a pointer:
#
#     move.c:192: error: assignment to 'NCURSES_BOOL' {aka 'unsigned char'}
#       from 'linestruct *' makes integer from pointer without a cast
#
# Every mainstream build (Arch's host ncurses included) ships
# NCURSES_ENABLE_STDBOOL_H 1.  It changes no ABI: bool and unsigned char have
# the same size and alignment under this compiler, checked with _Static_assert
# at both the default -std and -std=c99, so nothing already linked against
# libncurses needs rebuilding.
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --without-cxx-binding \
    --without-ada \
    --without-tests \
    --without-debug \
    --without-manpages \
    --with-shared \
    --with-normal \
    --with-termlib \
    --enable-overwrite \
    --disable-stripping \
    cf_cv_header_stdbool_h=1 \
    CFLAGS="-O2 -g -march=i486 -mtune=i486" \
    CPPFLAGS="-D_GNU_SOURCE"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  Staged at ${DESTDIR}/usr/lib/libncurses.so.6"

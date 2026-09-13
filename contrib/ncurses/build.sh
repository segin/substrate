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
# --enable-widec: build the wide-character library, libncursesw, with the
# cchar_t API (mvin_wchnstr, getcchar, setcchar, mvadd_wchnstr, ...).  mc
# 4.8.33 cannot be built without it -- its ncurses screen backend defines
# ENABLE_SHADOWS unconditionally and draws shadows with those calls -- and it
# is what gives nano and less multibyte (UTF-8) text.  Mainstream
# distributions ship only the wide build for the same reason.
#
# The wide build needed POSIX tsearch/tfind/tdelete from libc: extended colour
# pairs (NCURSES_EXT_COLORS, which --enable-widec turns on) are kept in a
# binary tree in ncurses/base/new_pair.c.
#
# --with-termlib=tinfo keeps the terminal-info library named libtinfo rather
# than libtinfow, so ports that link -ltinfo alone (gdb) are unaffected.
#
# The headers keep their plain names and, with --enable-overwrite, still
# install flat into /usr/include.
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
    --with-termlib=tinfo \
    --enable-widec \
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

# Link-time compatibility names.  Ports configure with -lncurses, -lcurses,
# -lform, -lmenu and -lpanel; with only the wide libraries staged those links
# would fail.  A one-line GNU ld script forwards each name to its wide library,
# so existing build systems link unchanged and record DT_NEEDED on the w
# library -- the same arrangement Arch uses.  These are link-time names only:
# there is deliberately no libncurses.so.6 runtime alias, because a binary
# built against the narrow headers is not ABI-compatible with libncursesw
# (WINDOW's layout differs), so it has to be rebuilt, not redirected.
for _l in ncurses form menu panel; do
    printf 'INPUT(-l%sw)\n' "${_l}" > "${DESTDIR}/usr/lib/lib${_l}.so"
    ln -sf "lib${_l}w.a" "${DESTDIR}/usr/lib/lib${_l}.a"
done
printf 'INPUT(-lncursesw)\n' > "${DESTDIR}/usr/lib/libcurses.so"
ln -sf libncursesw.a "${DESTDIR}/usr/lib/libcurses.a"

echo "==> Done.  Staged at ${DESTDIR}/usr/lib/libncursesw.so.6"

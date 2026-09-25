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
    --with-cxx-binding \
    --with-cxx-shared \
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

# The narrow libraries too: libncurses, libform, libmenu and libpanel as real
# shared objects (plus .a), so -lncurses, -lform, -lmenu, -lpanel and -lcurses
# each link a narrow library with its own libncurses.so.6-style soname, while
# the -l...w names link the wide ones -- the arrangement Debian ships.  A
# program that wants wide-character support links the w library explicitly
# (mc and nano do).
#
# One header set serves both: the wide build's.  It differs from the narrow
# one only in what NCURSES_WIDECHAR switches on, and WINDOW's wide-only
# members (_bkgrnd, _color) sit at the very end of struct _win_st under that
# switch, so the layout a narrow consumer sees -- NCURSES_WIDECHAR 0 unless it
# asks for _XOPEN_SOURCE_EXTENDED -- is the narrow library's.  Nor is a second
# libtinfo needed: the wide build's exports every symbol the narrow one does,
# plus the extended-colour *2 variants.
NARROW_DIR="${HERE}/build/build-stage-substrate-narrow"
NARROW_DEST="${HERE}/build/dest-narrow"
echo "==> configure (narrow)"
rm -rf "${NARROW_DIR}" "${NARROW_DEST}"; mkdir -p "${NARROW_DIR}"; cd "${NARROW_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --with-cxx-binding \
    --with-cxx-shared \
    --without-ada \
    --without-tests \
    --without-debug \
    --without-manpages \
    --with-shared \
    --with-normal \
    --with-termlib=tinfo \
    --enable-overwrite \
    --disable-stripping \
    cf_cv_header_stdbool_h=1 \
    CFLAGS="-O2 -g -march=i486 -mtune=i486" \
    CPPFLAGS="-D_GNU_SOURCE"

echo "==> make -j${JOBS} (narrow)"
make -j"${JOBS}"
make install DESTDIR="${NARROW_DEST}"

# Replaces the INPUT(-l...w) forwarding scripts earlier versions staged under
# the plain names.
for _l in ncurses form menu panel ncurses++; do
    rm -f "${DESTDIR}/usr/lib/lib${_l}.so" "${DESTDIR}/usr/lib/lib${_l}.a"
    cp -P "${NARROW_DEST}/usr/lib/lib${_l}.so"* "${NARROW_DEST}/usr/lib/lib${_l}.a" \
        "${DESTDIR}/usr/lib/"
done
ln -sf libncurses.so "${DESTDIR}/usr/lib/libcurses.so"
ln -sf libncurses.a "${DESTDIR}/usr/lib/libcurses.a"
rm -rf "${NARROW_DEST}"

echo "==> Done.  Staged libncurses.so.6 and libncursesw.so.6 under ${DESTDIR}/usr/lib"

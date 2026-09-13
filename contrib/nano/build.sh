#!/bin/sh
# contrib/nano/build.sh — cross-build GNU nano for substrate.
#
# Configured as a linux host so autoconf behaves; CC is the substrate cross
# gcc, so the output is a substrate binary once byte 7 of the ELF header is
# stamped with ELFOSABI_SUBSTRATE (0x40).
#
# Depends on ncurses (the screen), zlib and libmagic from contrib/file -- nano
# links the last two for compressed-file and syntax detection -- all staged in
# the cross sysroot first.
#
# Nothing in this port patches nano.  9.2 needed four substrate-side fixes to
# build, which is where they belong:
#   * <wchar.h> made self-contained: gnulib's replacement <stdint.h> includes it
#     mid-flight, and the old one's own <stdint.h> include collapsed.
#   * __fseterr() in libc: without it gnulib's fseterr.c stops the build with
#     "Please port gnulib fseterr.c to your platform!".
#   * st_atim/st_mtim in struct stat: files.c uses them unconditionally.
#   * ncurses built with NCURSES_ENABLE_STDBOOL_H 1: otherwise curses.h
#     redefines bool as unsigned char under nano's own <stdbool.h>.
#
# The screen library is narrow ncurses, so multibyte (UTF-8) editing waits on a
# wide-character ncurses build.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; PKG="nano"; VERSION="9.2"
TREE="${HERE}/build/nano-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${PKG}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

[ -f "${SR}/include/curses.h" ] || { echo "ncurses not staged -- build contrib/ncurses first" >&2; exit 1; }
[ -e "${SR}/lib/libz.so" ]      || { echo "zlib not staged -- build contrib/zlib first" >&2; exit 1; }
[ -e "${SR}/lib/libmagic.so" ]  || { echo "libmagic not staged -- build contrib/file first" >&2; exit 1; }

export PKG_CONFIG_LIBDIR="${SR}/lib/pkgconfig"
rm -rf "${BS}"; mkdir -p "${BS}"; cd "${BS}"
# --sysconfdir=/etc: the global nanorc belongs at /etc/nanorc, not the
# /usr/etc/nanorc that --prefix=/usr alone would imply.
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr --sysconfdir=/etc \
  --disable-nls \
  CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g" \
  LDFLAGS="-L${SR}/lib"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}/usr/share/info/dir"
for f in "${DESTDIR}"/usr/bin/*; do
    [ -f "$f" ] && [ ! -L "$f" ] && printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
done
echo "==> ${PKG} staged under ${DESTDIR}"

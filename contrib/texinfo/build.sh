#!/bin/sh
# contrib/texinfo/build.sh — cross-build GNU Texinfo for substrate.
#
# Configured as a linux host so autoconf behaves; CC is the substrate cross
# gcc, so the ELF programs are substrate binaries once byte 7 of the header is
# stamped with ELFOSABI_SUBSTRATE (0x40).
#
# Two kinds of program come out of this:
#   * C: info (the reader, on ncurses's libtinfo) and install-info.
#   * perl: texi2any, with makeinfo, pod2texi and texi2dvi around it.  These
#     are scripts whose #! line names /usr/bin/perl, so they run once
#     contrib/perl is on the image; that is why perl precedes this port.
#
# --disable-perl-xs: texi2any can load optional XS modules to go faster.
# Building them means compiling against the TARGET perl's headers with the
# target perl's own configuration, which a cross build does not have;
# without them texi2any uses its pure-perl implementation and produces the
# same output.
#
# The host perl is still needed at build time: configure finds it, and the
# build runs perl scripts to generate sources.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; PKG="texinfo"; VERSION="7.3"
TREE="${HERE}/build/texinfo-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${PKG}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

[ -f "${SR}/include/term.h" ]  || { echo "ncurses not staged -- build contrib/ncurses first" >&2; exit 1; }
command -v perl >/dev/null     || { echo "texinfo needs a host perl to build" >&2; exit 1; }

rm -rf "${BS}"; mkdir -p "${BS}"; cd "${BS}"
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr \
  --disable-perl-xs --disable-nls \
  CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g" \
  LDFLAGS="-L${SR}/lib"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
# The info directory file is assembled on the image from every package's
# pages, not shipped from one port.
rm -f "${DESTDIR}/usr/share/info/dir"
# Stamp the ELF programs only; the rest of /usr/bin is perl and sh scripts.
for f in "${DESTDIR}"/usr/bin/*; do
    [ -f "$f" ] && [ ! -L "$f" ] || continue
    [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] || continue
    printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
done
echo "==> ${PKG} staged under ${DESTDIR}"

#!/bin/sh
# contrib/gdb/build.sh — configure, build, STRIP and stage GNU gdb 16.2 for
# Substrate (Canadian cross: host = substrate, runs on the target).
#
# The -g build artifact (contrib/gdb/build/obj/gdb/gdb) is ~154 MiB; we ship the
# STRIPPED binary, which is ~8 MiB.  Always strip before staging so the image
# never carries the 154 MiB debug build.  See README.SUBSTRATE.md for the
# porting details (bfd vecs, configure.host/.nat, substrate-nat.c).
set -e

: "${SUBSTRATE_TOP:=$(cd "$(dirname "$0")/../.." && pwd)}"
: "${STAGE1_PREFIX:=/opt/substrate}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
SRCDIR="${SUBSTRATE_TOP}/contrib/gdb/build/gdb-16.2"
OBJDIR="${SUBSTRATE_TOP}/contrib/gdb/build/obj"
DESTDIR="${SUBSTRATE_TOP}/dist-overlay/dist-gdb"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"

if [ ! -x "${SRCDIR}/configure" ]; then
	echo "gdb sources missing — run contrib/gdb/fetch.sh first" >&2
	exit 1
fi

# --- configure + build ----------------------------------------------------
mkdir -p "${OBJDIR}"
cd "${OBJDIR}"
# Exported, not passed as configure arguments: the top-level configure does
# not forward arbitrary ac_cv_* assignments to the subdirectory configures,
# and gdb's own configure is the one that has to see them.  Subdir configures
# inherit the environment, and autoconf only sets a cache variable that is
# still unset, so exporting is what actually takes.
export ac_cv_header_curses_h=no ac_cv_header_cursesX_h=no
export ac_cv_header_ncurses_h=no ac_cv_header_ncurses_ncurses_h=no
export ac_cv_header_ncurses_term_h=no ac_cv_header_ncursesw_ncurses_h=no
export ac_cv_header_term_h=no
if [ ! -f Makefile ]; then
	"${SRCDIR}/configure" --build="$("${SRCDIR}/config.guess")" \
		--host=i386-unknown-substrate --target=i386-unknown-substrate \
		--prefix=/usr --disable-gdbserver --disable-werror --disable-nls \
		--without-guile --disable-tui --disable-source-highlight \
		--disable-shared --with-static-standard-libraries \
		--with-gmp="${SR}" --with-mpfr="${SR}"
fi
# The exported ac_cv_header_*curses* overrides above: gdb probes for those headers
# unconditionally, whatever --disable-tui and --with-curses say, and
# gdb_curses.h then includes <curses.h> into C++ translation units.
# Substrate's ncurses was cross-configured with NCURSES_ENABLE_STDBOOL_H=0
# -- ncurses cannot run its <stdbool.h> probe when cross-compiling and
# defaults it off -- so its curses.h takes the
#
#     #undef bool
#     #define bool NCURSES_BOOL
#
# branch, which is meant for pre-C99 compilers with no bool at all.  In C++
# that rewrites the keyword, and every gdb declaration of a `bool` global
# collides with its definition:
#
#     utils.c:110:6: error: conflicting declaration 'NCURSES_BOOL sevenbit_strings'
#
# A --disable-tui gdb only wanted curses for terminal sizing and falls back
# to termcap, so the contained fix is to not find the headers.  The ncurses
# side is a real defect that affects every C++ consumer of curses.h, but
# fixing it changes what `bool` means in that header and needs the whole
# ncurses-dependent stack rebuilt, so it is deliberately not done here.
# The sysroot .la files carry libdir=/usr/lib and misdirect libtool at the HOST
# libs (wrong format); drop them so -lgmp/-liconv resolve the substrate .so.
#
# contrib/gmp and contrib/mpfr each delete their own .la from the sysroot for
# this reason, but build.sh re-mirrors every dist-<pkg>/usr/lib into the
# sysroot after each port, which puts them straight back.  So drop them again
# here, at the consumer.  Missing libmpfr.la is what stopped the final link:
#
#   libtool: link: warning: library `.../lib/libmpfr.la' was moved.
#   libtool: link: cannot find the library `/usr/lib/libgmp.la'
#
# -- libmpfr.la's dependency_libs names /usr/lib/libgmp.la, the path libgmp.la
# would have had on the TARGET, which of course is not there on the builder.
rm -f "${SR}/lib/libgmp.la" "${SR}/lib/libmpfr.la" \
      "${SR}/lib/libiconv.la" "${SR}/lib/libcharset.la"
make all-gdb -k CXXFLAGS="-g -O2 -fpermissive"

# --- strip + OSABI-stamp + stage (8 MiB, NOT the 154 MiB -g binary) --------
mkdir -p "${DESTDIR}/usr/bin"
i386-unknown-substrate-strip gdb/gdb -o "${DESTDIR}/usr/bin/gdb"
# host cc -shared/-static stamps ELFOSABI_SYSV(0); the kernel exec dispatch
# needs ELFOSABI_SUBSTRATE(0x40) at e_ident[7].
printf '\100' | dd of="${DESTDIR}/usr/bin/gdb" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
echo "==> staged $(wc -c < "${DESTDIR}/usr/bin/gdb") byte (stripped) gdb into ${DESTDIR}"

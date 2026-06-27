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
if [ ! -f Makefile ]; then
	"${SRCDIR}/configure" --build="$("${SRCDIR}/config.guess")" \
		--host=i386-unknown-substrate --target=i386-unknown-substrate \
		--prefix=/usr --disable-gdbserver --disable-werror --disable-nls \
		--without-guile --disable-tui --disable-source-highlight \
		--disable-shared --with-static-standard-libraries \
		--with-gmp="${SR}" --with-mpfr="${SR}"
fi
# The sysroot .la files carry libdir=/usr/lib and misdirect libtool at the HOST
# libs (wrong format); drop them so -lgmp/-liconv resolve the substrate .so.
rm -f "${SR}/lib/libgmp.la" "${SR}/lib/libiconv.la" "${SR}/lib/libcharset.la"
make all-gdb -k CXXFLAGS="-g -O2 -fpermissive"

# --- strip + OSABI-stamp + stage (8 MiB, NOT the 154 MiB -g binary) --------
mkdir -p "${DESTDIR}/usr/bin"
i386-unknown-substrate-strip gdb/gdb -o "${DESTDIR}/usr/bin/gdb"
# host cc -shared/-static stamps ELFOSABI_SYSV(0); the kernel exec dispatch
# needs ELFOSABI_SUBSTRATE(0x40) at e_ident[7].
printf '\100' | dd of="${DESTDIR}/usr/bin/gdb" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
echo "==> staged $(wc -c < "${DESTDIR}/usr/bin/gdb") byte (stripped) gdb into ${DESTDIR}"

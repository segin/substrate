#!/bin/sh
# contrib/spandsp/build.sh — cross-build spandsp (G.722 + core DSP codecs) for substrate.
# Configured as a linux host (CC stays the substrate cross gcc) so the bundled
# libtool emits a shared library; the output is a substrate binary (OSABI 0x40).
#
# The FAX / T.4 / T.30 / T.38 stack is stripped (patches/0001) because it hard-
# requires libtiff, which substrate does not have.  PsyMP3 only needs the G.722
# codec (g722_encode / g722_decode / g722_*_init), which is in libspandsp core.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; LIB="spandsp"; VERSION="0.0.6"
TREE="${HERE}/build/spandsp-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
# Keep autotools maintainer-mode rules dormant: the 0001 patch edits Makefile.am
# (and spandsp.h.in), which would otherwise look newer than the generated
# Makefile.in / configure and trigger an automake/autoconf rerun we don't have.
# Touch the generated outputs newest, in dependency order.
( cd "${TREE}"
  touch configure.ac aclocal.m4 2>/dev/null || true
  sleep 1
  touch configure config.h.in src/spandsp.h.in 2>/dev/null || true
  find . -name 'Makefile.in' -exec touch {} + 2>/dev/null || true )
rm -rf "${BS}"; mkdir -p "${BS}"; cd "${BS}"
# ac_cv_func_{malloc,realloc}_0_nonnull=yes: AC_FUNC_MALLOC can't run its probe
# when cross-compiling, so it assumes a broken malloc and #defines malloc->rpl_malloc.
# That breaks the host-built make_at_dictionary tool (no rpl_malloc).  substrate's
# malloc(0) returns a unique pointer (glibc/musl convention), so the replacement
# is unnecessary.
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr --enable-shared --enable-static \
  ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes \
  CC=i386-unknown-substrate-gcc \
  CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la
# Stamp ELFOSABI_SUBSTRATE (0x40) on every real shared-object file.
for so in "${DESTDIR}"/usr/lib/*.so.*; do
  [ -f "$so" ] || continue
  case "$so" in *.so.*.*)
    printf '\100' | dd of="$so" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
    b=$(od -An -tx1 -j7 -N1 "$so" | tr -d ' ')
    [ "$b" = "40" ] || { echo "OSABI stamp failed on $so (got $b)" >&2; exit 1; }
    ;;
  esac
done
# Mirror libspandsp + headers + .pc into the cross sysroot so dependents (PsyMP3) find it.
mkdir -p "${SR}/lib/pkgconfig" "${SR}/include/spandsp"
cp -a "${DESTDIR}"/usr/lib/libspandsp.* "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/include/spandsp.h "${SR}/include/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/include/spandsp/. "${SR}/include/spandsp/" 2>/dev/null || true
cp "${DESTDIR}"/usr/lib/pkgconfig/spandsp.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true
echo "==> ${LIB} staged under ${DESTDIR}"

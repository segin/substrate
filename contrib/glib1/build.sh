#!/bin/sh
# contrib/glib1/build.sh — cross-compile GLib 1.2.10 for substrate.
#
# 2001-era autoconf runs test programs for several probes; the
# glib_cv_*/ac_cv_* preseeds below supply the substrate answers
# (i386, sane libc).  Threads are disabled: GTK+ 1.2 doesn't need
# gthread, and that drops the pthread-internals sizeof probes.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.2.10"
TREE_DIR="${HERE}/build/glib-${VERSION}"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-glib1}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"
[ -f Makefile ] && make distclean >/dev/null 2>&1 || true
# autoconf-2.13: VAR=VALUE configure args are not supported — pass the
# compiler and the cross preseeds through the environment instead.
export CC=i386-unknown-substrate-gcc
export CFLAGS="-O2 -std=gnu89 -w"
export glib_cv_sane_realloc=yes
export glib_cv_uscore=no
export glib_cv_va_val_copy=yes
export glib_cv_stack_grows=no
export glib_cv_rtldglobal_broken=no
export ac_cv_func_getpwuid_r=no
export ac_cv_sizeof_char=1
export ac_cv_sizeof_short=2
export ac_cv_sizeof_int=4
export ac_cv_sizeof_long=4
export ac_cv_sizeof_long_long=8
export ac_cv_sizeof_void_p=4
export glib_cv_has__inline=yes
export glib_cv_has__inline__=yes
export glib_cv_hasinline=yes
export glib_cv_va_copy=yes
export glib_cv___va_copy=yes
export ac_cv_func_getgrgid_r=no
./configure \
    --host=i386-unknown-substrate \
    --build=x86_64-pc-linux-gnu \
    --prefix=/usr \
    --disable-threads \
    --enable-shared --enable-static
make -j"${JOBS}"
rm -rf "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# Post-patch every produced .so OSABI byte to ELFOSABI_SUBSTRATE (0x40);
# substrate's cross-ld stamps SYSV otherwise (same as the X stack ports).
_n=0
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    _n=$((_n + 1))
done
echo "  OSABI->substrate on ${_n} shared objects"
echo "==> glib1 staged at ${DESTDIR}"

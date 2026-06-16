#!/bin/sh
# contrib/glib2/build.sh — cross-compile GLib 2.56.4 for substrate.
# 2.56 is the last autotools GLib; its glib-mkenums/glib-genmarshal are
# python scripts (run on the build host), avoiding a target-exec generator.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.56.4"; TREE_DIR="${HERE}/build/glib-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-glib2}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
ZL="${SUBSTRATE_TOP}/dist-overlay/dist-zlib"; FFI="${SUBSTRATE_TOP}/dist-overlay/dist-libffi"
for d in "${ZL}" "${FFI}"; do [ -d "${d}/usr" ] || { echo "build.sh: ${d} missing" >&2; exit 1; }; done

export CPPFLAGS="-I${ZL}/usr/include -I${FFI}/usr/include"
export LDFLAGS="-L${ZL}/usr/lib -L${FFI}/usr/lib -Wl,-rpath-link,${ZL}/usr/lib -Wl,-rpath-link,${FFI}/usr/lib -Wl,--copy-dt-needed-entries"
export PKG_CONFIG_LIBDIR="${ZL}/usr/lib/pkgconfig:${FFI}/usr/lib/pkgconfig"
export LIBFFI_CFLAGS="-I${FFI}/usr/include"
export LIBFFI_LIBS="-L${FFI}/usr/lib -lffi"
export ZLIB_CFLAGS="-I${ZL}/usr/include"
export ZLIB_LIBS="-L${ZL}/usr/lib -lz"
export PYTHON=python3

# Cross run-test cache preseeds (substrate = i386, glibc-like libc).
export glib_cv_stack_grows=no glib_cv_uscore=no \
       glib_cv_have_qsort_r=yes glib_cv_long_long_format=ll \
       ac_cv_func_posix_getpwuid_r=yes ac_cv_func_posix_getgrgid_r=yes \
       ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes \
       glib_cv_compliant_posix_memalign=1 \
       ac_cv_func_printf_unix98=yes ac_cv_func_vsnprintf_c99=yes \
       gl_cv_func_printf_positions=yes \
       glib_cv_rtldglobal_broken=no glib_cv_va_val_copy=yes

substrate_libtool_fix "${TREE_DIR}/configure"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --with-pcre=internal --disable-libmount --disable-selinux \
    --enable-compile-warnings=no \
    --disable-dtrace --disable-systemtap --disable-fam --disable-man --disable-gtk-doc \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -std=gnu11"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
# automake's py-compile imports the removed `imp` module; shim get_tag().
export PYTHONPATH="${HERE}/../automake-pyshim${PYTHONPATH:+:${PYTHONPATH}}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> glib2 staged at ${DESTDIR}"

#!/bin/sh
# contrib/gtk2/build.sh — cross-compile GTK+ 2.24.33 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.24.33"; TREE_DIR="${HERE}/build/gtk+-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-gtk2}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

substrate_sysroot "${HERE}/build/sysroot" \
    glib2 libffi zlib freetype libpng expat fontconfig harfbuzz fribidi \
    pixman cairo pango atk gdk-pixbuf \
    xorgproto libXau xtrans libxcb libX11 libXext libXrender
export PYTHON=python3
export LDFLAGS="${LDFLAGS} -ldl"
export CFLAGS="-march=i486 -mtune=i486 -O2 -g -std=gnu11 -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=return-mismatch -Wno-error=format -Wno-error=discarded-qualifiers"
# gtk's gdk-pixbuf-csource embeds the stock cursors at build time; it links
# gdk-pixbuf which needs the included-png-loader + libpng/z — handled by the
# sysroot.  Cross run-tests + substrate malloc(0):
export ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes \
       ac_cv_path_GDK_PIXBUF_CSOURCE="${SUBSTRATE_TOP}/dist-gdk-pixbuf/usr/bin/gdk-pixbuf-csource"

substrate_libtool_fix "${TREE_DIR}/configure"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include --sysconfdir=/etc \
    --enable-shared --enable-static \
    --with-x --x-includes="${HERE}/build/sysroot/usr/include" --x-libraries="${HERE}/build/sysroot/usr/lib" \
    --disable-introspection --disable-glibtest --disable-gtk-doc --disable-cups \
    --disable-papi --without-libjasper \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc
# Drop demos/tests/perf: they run the cross-built gdk-pixbuf-csource to inline
# PNGs at build time, which can't execute on the host.  The libraries
# (gdk + gtk + loader modules) and translations are what we install.
sed -i 's/^SRC_SUBDIRS = gdk gtk modules demos tests perf/SRC_SUBDIRS = gdk gtk modules/' Makefile
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
export PYTHONPATH="${HERE}/../automake-pyshim${PYTHONPATH:+:${PYTHONPATH}}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> gtk2 staged at ${DESTDIR}"

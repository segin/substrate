#!/bin/sh
# contrib/gdk-pixbuf/build.sh — cross-compile gdk-pixbuf 2.36.12 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.36.12"; TREE_DIR="${HERE}/build/gdk-pixbuf-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-gdk-pixbuf}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

substrate_sysroot "${HERE}/build/sysroot" glib2 libffi zlib libpng
# shared-mime-info ships only data (MIME globs) + a version .pc; gdk-pixbuf
# checks it for content-type detection.  Stub the .pc so configure passes
# (substrate has no shared-mime-info port; content-type calls degrade
# gracefully without the database).
cat > "${HERE}/build/sysroot/usr/lib/pkgconfig/shared-mime-info.pc" <<'PC'
Name: shared-mime-info
Description: stub (substrate has no shared-mime-info database)
Version: 2.0
PC
export PYTHON=python3
export LDFLAGS="${LDFLAGS} -ldl"
export CFLAGS="-march=i486 -mtune=i486 -O2 -g -std=gnu11"

substrate_libtool_fix "${TREE_DIR}/configure"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
# --disable-modules builds the loaders INTO libgdk_pixbuf (no separate .so
# loaders, so no target gdk-pixbuf-query-loaders run at build time).  PNG is
# the loader GTK themes need; jpeg/tiff/etc. are off (no ports yet).
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --disable-modules --with-included-loaders=png \
    --disable-introspection --disable-glibtest --disable-gtk-doc \
    --without-libtiff --without-libjpeg --without-libjasper \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc
# Trim the top-level SUBDIRS to the library (+ po): thumbnailer/ needs a
# loaders.cache that --disable-modules never produces; docs/tests/contrib
# aren't runtime.  Edit the generated Makefile (a SUBDIRS= make override
# would wrongly propagate into the recursive sub-makes).
sed -i 's/^SUBDIRS = gdk-pixbuf po docs thumbnailer tests contrib win32/SUBDIRS = gdk-pixbuf po/' Makefile
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
export PYTHONPATH="${HERE}/../automake-pyshim${PYTHONPATH:+:${PYTHONPATH}}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> gdk-pixbuf staged at ${DESTDIR}"

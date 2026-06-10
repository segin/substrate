#!/bin/sh
# contrib/gtk1/build.sh — cross-compile GTK+ 1.2.10 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.2.10"
TREE_DIR="${HERE}/build/gtk+-${VERSION}"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-gtk1}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Assemble a sysroot from the prerequisite dist trees (X stack + glib1).
SR="${HERE}/build/sysroot"
rm -rf "${SR}"; mkdir -p "${SR}/usr/lib"
for d in xorgproto libXau xtrans libxcb libX11 libXext glib1; do
    st="${SUBSTRATE_TOP}/dist-${d}"
    [ -d "${st}/usr" ] || { echo "build.sh: ${st} missing — build contrib/${d} first" >&2; exit 1; }
    cp -a "${st}/usr/." "${SR}/usr/"
done
# substrate core libs + unversioned link names
for l in c sys m; do
    cp "${SUBSTRATE_TOP}/lib/${l}/lib${l}.so.0" "${SR}/usr/lib/" 2>/dev/null || true
    ln -sf "lib${l}.so.0" "${SR}/usr/lib/lib${l}.so"
done

cd "${TREE_DIR}"
[ -f Makefile ] && make distclean >/dev/null 2>&1 || true
export CC=i386-unknown-substrate-gcc
export CFLAGS="-O2 -std=gnu89 -w"
export CPPFLAGS="-I${SR}/usr/include -I${SR}/usr/include/X11"
export LDFLAGS="-L${SR}/usr/lib -Wl,-rpath-link,${SR}/usr/lib"
# glib-config hardcodes prefix=/usr; relocate a copy to the sysroot so its
# emitted -I/-L point at the cross headers/libs, and hand it to configure.
mkdir -p "${SR}/usr/bin"
sed "s|^prefix=/usr$|prefix=${SR}/usr|" \
    "${SUBSTRATE_TOP}/dist-glib1/usr/bin/glib-config" > "${SR}/usr/bin/glib-config"
chmod +x "${SR}/usr/bin/glib-config"
export GLIB_CONFIG="${SR}/usr/bin/glib-config"
PATH="${SR}/usr/bin:${PATH}"; export PATH
# cross run-test preseeds (same family as glib)
export ac_cv_sizeof_char=1 ac_cv_sizeof_short=2 ac_cv_sizeof_int=4 \
       ac_cv_sizeof_long=4 ac_cv_sizeof_long_long=8 ac_cv_sizeof_void_p=4
export gtk_cv_sys_socklen_t=socklen_t

./configure \
    --host=i386-unknown-substrate \
    --build=x86_64-pc-linux-gnu \
    --prefix=/usr \
    --enable-shared --enable-static \
    --x-includes="${SR}/usr/include" \
    --x-libraries="${SR}/usr/lib"
# Build only the libraries (gdk + gtk).  po/ (message catalogs) needs the
# host msgfmt to accept 1999-era charset names like "iso-8859-9e" which
# modern gettext rejects, and docs/ needs sgml tooling — neither is part of
# the runtime.  The top-level install hooks (gtk-config, gtk.m4) still run.
make -j"${JOBS}" SUBDIRS="gdk gtk"
rm -rf "${DESTDIR}"
make install DESTDIR="${DESTDIR}" SUBDIRS="gdk gtk"
_n=0
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue; [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    _n=$((_n + 1))
done
echo "  OSABI->substrate on ${_n} shared objects"
echo "==> gtk1 staged at ${DESTDIR}"

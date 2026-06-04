#!/bin/sh
# contrib/glib/build.sh — cross-build GLib 2.56.4 for substrate (i386).
#
# Dependencies (build these first):
#   contrib/libffi   -> dist-libffi   (GObject FFI; mandatory)
#   contrib/zlib     -> dist-zlib     (GZlib streams)
#   contrib/libiconv -> dist-libiconv (charset conversion)
# PCRE is bundled (--with-pcre=internal); NLS is disabled.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.56.4"
TREE_DIR="${HERE}/build/glib-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-glib}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

FFI="${SUBSTRATE_TOP}/dist-libffi/usr"
ZLIB="${SUBSTRATE_TOP}/dist-zlib/usr"
ICONV="${SUBSTRATE_TOP}/dist-libiconv/usr"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
# Constrain pkg-config to the substrate dependency trees ONLY.  Without this it
# searches the host /usr/lib/pkgconfig and silently links the cross build
# against host libraries (it was picking up host libelf, enabling HAVE_LIBELF
# for a target that has no libelf).
export PKG_CONFIG_LIBDIR="${FFI}/lib/pkgconfig:${ZLIB}/lib/pkgconfig:${ICONV}/lib/pkgconfig"
export PKG_CONFIG_PATH=""
# Use a throwaway copy of the cross cache so configure can rewrite env vars into
# it without poisoning the committed seed cache (the env-consistency check trips
# whenever CFLAGS change between runs).  Strip any ac_cv_env_* just in case.
CACHE="${BUILD_DIR}/config.cache"
grep -v '^ac_cv_env_' "${HERE}/substrate-cross.cache" > "${CACHE}"
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --enable-shared --enable-static \
    --with-pcre=internal \
    --disable-nls \
    --disable-libmount \
    --disable-selinux \
    --disable-dtrace \
    --disable-systemtap \
    --disable-fam \
    --cache-file="${CACHE}" \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -std=gnu11 -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=format-overflow -Wno-error=format-truncation -Wno-error=format -Wno-error=format-nonliteral -Wno-error=format-security -I${FFI}/include -I${ZLIB}/include -I${ICONV}/include" \
    CPPFLAGS="-I${FFI}/include -I${ZLIB}/include -I${ICONV}/include" \
    LDFLAGS="-L${FFI}/lib -L${ZLIB}/lib -L${ICONV}/lib" \
    LIBFFI_CFLAGS="-I${FFI}/include" \
    LIBFFI_LIBS="-L${FFI}/lib -lffi" \
    ZLIB_CFLAGS="-I${ZLIB}/include" \
    ZLIB_LIBS="-L${ZLIB}/lib -lz"
echo "==> make -j${JOBS}"
make -j"${JOBS}"
# glib 2.56's automake py-compile helper does `import imp`, a module removed in
# Python 3.12+.  It only byte-compiles the installed gdbus-codegen .py files
# (which still install fine without it), so neutralize it to let install finish.
printf '#!/bin/sh\nexit 0\n' > "${TREE_DIR}/py-compile"
echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
echo "==> Done.  Staged at ${DESTDIR}/usr/{lib/libglib-2.0*,lib/libgobject-2.0*,include/glib-2.0}"

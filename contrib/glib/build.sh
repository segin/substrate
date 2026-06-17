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
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-glib}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

FFI="${SUBSTRATE_TOP}/dist-overlay/dist-libffi/usr"
ZLIB="${SUBSTRATE_TOP}/dist-overlay/dist-zlib/usr"
ICONV="${SUBSTRATE_TOP}/dist-overlay/dist-libiconv/usr"

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
# Teach GLib's libtool that substrate builds ELF shared libraries, otherwise
# --enable-shared yields only the .a archives (libtool's host_os case has no
# substrate branch -> build_libtool_libs=no).
sh "${HERE}/../substrate-libtool-shared.sh" "${TREE_DIR}/configure"
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
# Drop libtool archives: their absolute libdir=/usr/lib makes a downstream cross
# link resolve -lglib-2.0 to the build host's library ("file in wrong format").
rm -f "${DESTDIR}"/usr/lib/*.la

# Record proper DT_NEEDED on the glib stack.  substrate's `gcc -shared`
# adds no implicit libc, and glib 2.56's libtool defers ALL deplibs (the
# inter-glib .la deps AND the system libs: pthread/m/z/iconv/ffi/dl) to
# the .la, so the installed shared objects come out with an EMPTY
# DT_NEEDED -> ld.so cannot resolve pthread_* et al at runtime.  Relink
# each library from its objects with explicit deps, in dependency order.
echo "==> Relinking glib stack with explicit DT_NEEDED"
SR="${STAGE1_PREFIX}/i386-unknown-substrate/lib"
LD="${DESTDIR}/usr/lib"
VER="0.5600.4"
_glib_relink() {   # $1=basename(glib/gobject/...)  $2..=extra link args
    _b="$1"; shift
    _objs=$(find "${BUILD_DIR}/${_b}" -path '*/.libs/*.o' -name "lib${_b}_2_0_la-*.o")
    i386-unknown-substrate-gcc -shared -fPIC -Wl,-z,nodelete \
        -L"${SR}" -L"${LD}" \
        -Wl,-soname,"lib${_b}-2.0.so.0" -o "${LD}/lib${_b}-2.0.so.${VER}" \
        ${_objs} "$@"
    ( cd "${LD}" && ln -sf "lib${_b}-2.0.so.${VER}" "lib${_b}-2.0.so.0" \
                 && ln -sf "lib${_b}-2.0.so.${VER}" "lib${_b}-2.0.so" )
}
_glib_relink glib \
    -Wl,--whole-archive "${BUILD_DIR}/glib/libcharset/.libs/libcharset.a" \
                        "${BUILD_DIR}/glib/pcre/.libs/libpcre.a" -Wl,--no-whole-archive \
    -L"${ICONV}/lib" -liconv -L"${ZLIB}/lib" -lz \
    -Wl,--no-as-needed -l:libpthread.so.0 -lm -l:libc.so.0
_glib_relink gthread -lglib-2.0 -l:libpthread.so.0 -l:libc.so.0
_glib_relink gmodule -lglib-2.0 -l:libdl.so.0 -l:libc.so.0
_glib_relink gobject -lglib-2.0 -L"${FFI}/lib" -lffi -l:libc.so.0
_glib_relink gio     -lglib-2.0 -lgobject-2.0 -lgmodule-2.0 -L"${ZLIB}/lib" -lz -l:libc.so.0

# Stamp ELFOSABI_SUBSTRATE on the relinked shared objects.
for _so in "${LD}"/lib{glib,gobject,gio,gmodule,gthread}-2.0.so.${VER}; do
    [ -f "${_so}" ] && printf '\100' | dd of="${_so}" bs=1 seek=7 count=1 conv=notrunc status=none
done

echo "==> Done.  Staged at ${DESTDIR}/usr/{lib/libglib-2.0*,lib/libgobject-2.0*,include/glib-2.0}"

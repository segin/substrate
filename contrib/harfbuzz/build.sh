#!/bin/sh
# contrib/harfbuzz/build.sh — cross-compile HarfBuzz 14.6.0 for substrate.
#
# HarfBuzz builds with Meson only (its autotools build went in 8.x), so this
# writes a Meson cross file for the substrate toolchain and builds against a
# sysroot assembled from the staged dependencies (substrate_sysroot), the
# way the autotools build did.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.6.0"; TREE_DIR="${HERE}/build/harfbuzz-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-harfbuzz}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

SR="${HERE}/build/sysroot"
substrate_sysroot "${SR}" glib2 libffi zlib freetype libpng

# Meson's threads dependency passes -pthread, which substrate's gcc driver
# does not have (pthreads live in libpthread, as for cairo and lmdb).  The
# compilers meson runs are wrappers that spell it -D_REENTRANT -lpthread.
WRAP="${HERE}/build/wrap"; mkdir -p "${WRAP}"
for tool in gcc g++; do
    cat > "${WRAP}/${tool}" <<EOF
#!/bin/sh
for a; do
    shift
    if [ "\$a" = -pthread ]; then set -- "\$@" -D_REENTRANT -lpthread; else set -- "\$@" "\$a"; fi
done
exec ${STAGE1_PREFIX}/bin/i386-unknown-substrate-${tool} "\$@"
EOF
    chmod +x "${WRAP}/${tool}"
done

# Shared objects get libc and libsys named explicitly: substrate's
# `gcc -shared` adds no implicit libc, and libsys owns the syscall wrappers.
CROSS_FILE="${HERE}/build/substrate-cross.ini"
cat > "${CROSS_FILE}" <<EOF
[binaries]
c = '${WRAP}/gcc'
cpp = '${WRAP}/g++'
ar = 'i386-unknown-substrate-ar'
strip = 'i386-unknown-substrate-strip'
pkg-config = 'pkg-config'

[host_machine]
system = 'substrate'
cpu_family = 'x86'
cpu = 'i486'
endian = 'little'

[properties]
pkg_config_libdir = '${SR}/usr/lib/pkgconfig'
# sys_root makes meson export PKG_CONFIG_SYSROOT_DIR, so the -I/-L in the
# staged .pc files (prefix=/usr) resolve inside the sysroot, not on the host.
sys_root = '${SR}'

[built-in options]
c_args = ['-march=i486', '-mtune=i486', '-O2', '-g']
cpp_args = ['-march=i486', '-mtune=i486', '-O2', '-g']
c_link_args = ['-L${SR}/usr/lib', '-Wl,-rpath-link,${SR}/usr/lib', '-lc', '-lsys']
cpp_link_args = ['-L${SR}/usr/lib', '-Wl,-rpath-link,${SR}/usr/lib', '-lc', '-lsys']
EOF

rm -rf "${BUILD_DIR}"
meson setup "${BUILD_DIR}" "${TREE_DIR}" \
    --cross-file "${CROSS_FILE}" \
    --prefix=/usr --libdir=lib --includedir=include \
    --buildtype=plain -Ddefault_library=both -Db_pie=false \
    -Dglib=enabled -Dfreetype=enabled \
    -Dgobject=disabled -Dcairo=disabled -Dchafa=disabled -Dicu=disabled \
    -Dpng=disabled -Dzlib=disabled \
    -Draster=disabled -Dvector=disabled -Dgpu=disabled -Dgpu_demo=disabled \
    -Dsubset=enabled \
    -Dtests=disabled -Dintrospection=disabled -Ddocs=disabled \
    -Dutilities=disabled -Dbenchmark=disabled
ninja -C "${BUILD_DIR}" -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
DESTDIR="${DESTDIR}" meson install -C "${BUILD_DIR}" --no-rebuild
substrate_so_finalize "${DESTDIR}"
echo "==> harfbuzz staged at ${DESTDIR}"

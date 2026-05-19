#!/bin/sh
#
# build.sh — cross-build libarchive + bsdtar for substrate.
#
# Configures with --without- on every optional dependency by
# default; we'll re-enable bzip2 (already ported) once the bare
# build is known-good, and add zlib/xz/zstd as they land.  Disables
# bsdcpio and bsdcat — substrate just needs tar — but the library
# itself is left fully functional and ready to feed those clients
# later.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-libarchive)
#   JOBS            parallel jobs (default `nproc`)
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="3.7.7"
TREE_DIR="${HERE}/build/libarchive-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-libarchive}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Optional backends: enable each one only if its sibling contrib
# port has been staged.  This keeps the libarchive build idempotent
# whether you've built bzip2/zlib/xz first or not — substrate's
# contribs each stage under dist-<name>/usr/, so a `[ -d ... ]`
# test on the staged tree is the cleanest gate.  CFLAGS / LDFLAGS
# pick up sibling headers + libs without polluting the live
# substrate sysroot.
ARCHIVE_BACKEND_CFLAGS=""
ARCHIVE_BACKEND_LDFLAGS=""
ARCHIVE_BZIP2_FLAG="--without-bz2lib"
if [ -f "${SUBSTRATE_TOP}/dist-bzip2/usr/include/bzlib.h" ] &&
   [ -f "${SUBSTRATE_TOP}/dist-bzip2/usr/lib/libbz2.so.1.0.8" ]; then
    ARCHIVE_BACKEND_CFLAGS="${ARCHIVE_BACKEND_CFLAGS} -I${SUBSTRATE_TOP}/dist-bzip2/usr/include"
    ARCHIVE_BACKEND_LDFLAGS="${ARCHIVE_BACKEND_LDFLAGS} -L${SUBSTRATE_TOP}/dist-bzip2/usr/lib"
    ARCHIVE_BZIP2_FLAG=""
    echo "==> bzip2 backend: ON (dist-bzip2 found)"
else
    echo "==> bzip2 backend: OFF (run contrib/bzip2/build.sh first)"
fi

# Many backends auto-detect at configure time; explicitly disable
# anything substrate hasn't ported yet so the resulting binary
# doesn't link against missing host libs.  Re-enable a backend by
# dropping its --without- flag once its library is on the rootfs.
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --build=x86_64-pc-linux-gnu \
    --prefix=/usr \
    --disable-static \
    --enable-shared \
    --disable-bsdcpio \
    --disable-bsdcat \
    --disable-bsdunzip \
    --without-zlib \
    ${ARCHIVE_BZIP2_FLAG} \
    --without-libb2 \
    --without-iconv \
    --without-lz4 \
    --without-zstd \
    --without-lzma \
    --without-lzo2 \
    --without-cng \
    --without-openssl \
    --without-mbedtls \
    --without-nettle \
    --without-xml2 \
    --without-expat \
    --disable-posix-regex-lib \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie ${ARCHIVE_BACKEND_CFLAGS}" \
    LDFLAGS="-fno-pie ${ARCHIVE_BACKEND_LDFLAGS}"

make -j"${JOBS}"

echo "==> Installing into ${DESTDIR}"
rm -rf "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# Post-patch every produced .so OSABI byte to ELFOSABI_SUBSTRATE
# (64).  substrate's cross-ld stamps SYSV otherwise; this mirrors
# what lib/c does for libc.so.0.
for so in "${DESTDIR}"/usr/lib/*.so.*.*; do
    [ -f "${so}" ] || continue
    printf '\x40' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    echo "  OSABI->substrate on $(basename "${so}")"
done

# Drop the static-build leftovers we asked configure not to make,
# in case it ignored us.
rm -f "${DESTDIR}"/usr/lib/*.la

# libtool decides shared libs are off for host_os=substrate (its
# deplibs_check_method comes back "unknown" for our triple and that
# combined with other autoconf-libtool host-OS gates flips
# build_libtool_libs=no).  We compensate by building bsdtar with
# the static libarchive.a linked in — the bsdtar binary is fully
# functional on its own and the .a only matters for downstream
# C consumers.  Drop the .a from the rootfs to save space — the
# image only needs the binary.  Keep the headers around so users
# building against libarchive locally on the target still work.
rm -f "${DESTDIR}"/usr/lib/libarchive.a

# Install bsdtar as the system tar.  Substrate's in-tree bin/tar
# is buggy (directory-entry extract + verbose format breakage);
# bsdtar is the replacement.  Keep both names so scripts that
# explicitly call `bsdtar` keep working.
ln -sf bsdtar "${DESTDIR}/usr/bin/tar"

echo "==> Done.  Files staged in ${DESTDIR}/usr/{bin,lib,include}"

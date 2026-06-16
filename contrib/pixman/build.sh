#!/bin/sh
#
# contrib/pixman/build.sh — cross-build pixman for substrate.
# Produces:
#   /usr/lib/libpixman-1.a + libpixman-1.so.0 + libpixman-1.so
#   /usr/include/pixman-1/pixman.h + pixman-version.h
#   /usr/lib/pkgconfig/pixman-1.pc
#
# pixman is the pixel-region helper library every X server and a
# lot of toolkits (cairo, ...) link against.  Required by xorg-server.
#
# Env:
#   STAGE1_PREFIX     substrate toolchain prefix (default /opt/substrate)
#   DESTDIR           staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-pixman)
#   JOBS              parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="0.42.2"
TREE_DIR="${HERE}/build/pixman-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-pixman}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Pixman options:
#   --disable-mmx / --disable-sse2: substrate's userland is i486
#     baseline (-march=i486 in libc), no SSE.  Disable the
#     MMX/SSE2 fast paths even though configure's autodetect
#     would otherwise pick them up from the host's CPU.
#   --disable-arm-*: not relevant on i386 but configure complains
#     less if explicit.
#   --disable-openmp: no OpenMP runtime in substrate libc.
#   --disable-gtk: avoids pulling in glib for the demo programs.
#   --disable-libpng: avoids libpng for the demo programs.
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --includedir=/usr/include \
    --enable-shared \
    --enable-static \
    --disable-mmx \
    --disable-sse2 \
    --disable-ssse3 \
    --disable-arm-simd \
    --disable-arm-neon \
    --disable-arm-iwmmxt \
    --disable-openmp \
    --disable-gtk \
    --disable-libpng \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LDFLAGS="-fno-pie"

echo "==> make -j${JOBS} (pixman + headers only — skip tests/demos)"
# The test/ and demos/ subdirs pull in fenv.h and other host-leaning
# headers that the substrate libc fixed-include chain mishandles
# (gcc include-fixed/fenv.h has parse errors for noinline attrs on
# substrate).  We only need libpixman-1 for downstream consumers
# (xorg-server, cairo, ...).  Build the lib subdir explicitly.
make -j"${JOBS}" -C pixman

echo "==> install lib + headers into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make -C pixman install DESTDIR="${DESTDIR}"
make install-pkgconfigDATA DESTDIR="${DESTDIR}" 2>/dev/null || true

# Drop libtool archives — substrate links against .a / .so directly.
rm -f "${DESTDIR}"/usr/lib/*.la

# Post-patch every produced .so OSABI byte to ELFOSABI_SUBSTRATE (0x40).
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    echo "  OSABI->substrate on $(basename "${so}")"
done

echo "==> Done.  pixman staged under ${DESTDIR}"

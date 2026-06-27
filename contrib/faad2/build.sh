#!/bin/sh
# contrib/faad2/build.sh — cross-build libfaad (AAC decoder) for substrate.
# faad2 2.11.1 is CMake-only.  We configure as a linux host (CMAKE_SYSTEM_NAME=Linux)
# so CMake emits a versioned shared library, while CMAKE_C_COMPILER stays the
# substrate cross gcc so the output objects are substrate ELF (OSABI 0x40, stamped
# below).  PsyMP3 consumes this via PKG_CHECK_MODULES([AAC],[faad2]) -> -lfaad.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"; PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
VER="2.11.1"; TREE="${HERE}/build/faad2-${VER}"; DEST="${SUBSTRATE_TOP}/dist-overlay/dist-faad2"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
cd "${TREE}"; rm -rf bld; mkdir bld; cd bld
cmake -G "Unix Makefiles" \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=i386 \
    -DCMAKE_C_COMPILER=i386-unknown-substrate-gcc \
    -DCMAKE_C_FLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector" \
    -DCMAKE_EXE_LINKER_FLAGS="-L${SR}/lib -l:libc.so.0" \
    -DCMAKE_SHARED_LINKER_FLAGS="-L${SR}/lib -l:libc.so.0" \
    -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_SHARED_LIBS=ON ..
make -j"${JOBS}"
rm -rf "${DEST}"; make install DESTDIR="${DEST}"
find "${DEST}" -name '*.la' -delete

# Stamp ELFOSABI_SUBSTRATE (0x40) at e_ident[7] on every real shared object.
for so in $(find "${DEST}/usr" -name '*.so.*.*' -type f 2>/dev/null); do
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    b=$(od -An -tx1 -j7 -N1 "${so}" | tr -d ' ')
    [ "${b}" = "40" ] || { echo "OSABI stamp failed on ${so} (got ${b})" >&2; exit 1; }
done

# Mirror libs + headers + .pc into the cross sysroot so dependent ports
# (PsyMP3's PKG_CHECK_MODULES) find them.
mkdir -p "${SR}/lib/pkgconfig"
cp -a "${DEST}"/usr/lib/libfaad*.so* "${SR}/lib/" 2>/dev/null || true
cp -a "${DEST}"/usr/include/neaacdec.h "${DEST}"/usr/include/faad.h "${SR}/include/" 2>/dev/null || true
cp -a "${DEST}"/usr/lib/pkgconfig/faad2.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true
echo "==> faad2 ${VER} staged under ${DEST}/usr and mirrored into ${SR}"

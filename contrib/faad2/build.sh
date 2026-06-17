#!/bin/sh
# contrib/faad2/build.sh — cross-build libfaad (AAC decoder) for substrate.
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
for so in $(find "${DEST}/usr" -name '*.so.*' -type f 2>/dev/null); do
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
done
cp -a "${DEST}"/usr/lib*/libfaad*.so* "${SR}/lib/" 2>/dev/null || true
cp -an "${DEST}"/usr/include/*.h "${SR}/include/" 2>/dev/null || true
echo "==> faad2 staged under ${DEST}/usr"

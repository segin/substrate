#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"; PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
BINU="$(ls -d "${SUBSTRATE_TOP}"/contrib/binutils/build/binutils-*/ | head -1)"
cfgsub() { for s in config.sub config.guess; do find . -name "$s" -exec cp -f "${BINU}/$s" {} + ; done
  sh "${SUBSTRATE_TOP}/contrib/substrate-libtool-shared.sh" ./configure >/dev/null 2>&1 || true; }
osabi_mirror() { # $1=DEST  (stamp .so + mirror libs/headers/pc to sysroot)
  find "$1" -name "*.la" -delete
  for so in $(find "$1/usr/lib" -name "*.so.*" -type f 2>/dev/null); do printf "\100" | dd of="$so" bs=1 seek=7 count=1 conv=notrunc status=none; done
  cp -a "$1"/usr/lib/*.so* "${SR}/lib/" 2>/dev/null || true
  cp -an "$1"/usr/include/* "${SR}/include/" 2>/dev/null || true
  cp -a "$1"/usr/lib/pkgconfig/*.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true; }
TREE="${HERE}/build/libxml2-2.11.9"; DEST="${SUBSTRATE_TOP}/dist-overlay/dist-libxml2"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
cd "${TREE}"; cfgsub
./configure --host=i386-unknown-substrate --prefix=/usr --enable-shared --enable-static \
    --without-python --without-zlib --without-lzma --without-iconv --without-icu --without-debug \
    CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" LDFLAGS="-L${SR}/lib"
make -j"${JOBS}"; rm -rf "${DEST}"; make install DESTDIR="${DEST}"
osabi_mirror "${DEST}"
echo "==> libxml2 staged under ${DEST}/usr"

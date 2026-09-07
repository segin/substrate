#!/bin/sh
# contrib/file/build.sh — cross-build libmagic (file 5.45) for substrate.
# tdelibs links libmagic for MIME detection.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"; PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
TREE="${HERE}/build/file-5.45"; DEST="${SUBSTRATE_TOP}/dist-overlay/dist-file"
# config.sub via the shared helper, not a copy out of
# contrib/binutils/build/binutils-*/: that tree only exists when the
# toolchain was built this run, and build.sh sets SKIP_TOOLCHAIN=1 on a CI
# toolchain-cache hit.  The old `ls -d ... | head -1` then returned nothing
# and the build died on `cp -f /config.sub`.
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
cd "${TREE}"
substrate_config_sub_fix "."
sh "${SUBSTRATE_TOP}/contrib/substrate-libtool-shared.sh" ./configure >/dev/null 2>&1 || true
# substrate keeps POSIX regcomp/regexec in libregex (not libc) -> -lregex.
./configure --host=i386-unknown-substrate --prefix=/usr --enable-shared --enable-static \
    --disable-zlib --disable-bzlib --disable-xzlib --disable-zstd --disable-lzlib --disable-seccomp \
    CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LDFLAGS="-L${SR}/lib" LIBS="-lregex"
# magic.mgc is compiled by a runnable `file`; cross-built file can't run on
# the host, so use the host's file(1) as the magic compiler.
make -j"${JOBS}" FILE_COMPILE=/usr/bin/file
rm -rf "${DEST}"; make install DESTDIR="${DEST}" FILE_COMPILE=/usr/bin/file
rm -f "${DEST}"/usr/lib/*.la
# libtool drops the regex deplib; relink libmagic.so from its archive so it
# records libregex in DT_NEEDED (else ld.so can't resolve regcomp at load).
i386-unknown-substrate-gcc -shared -fPIC -Wl,-z,nodelete -Wl,-soname,libmagic.so.1 \
    -Wl,--whole-archive src/.libs/libmagic.a -Wl,--no-whole-archive \
    -L"${SR}/lib" -Wl,--no-as-needed -l:libregex.so.0 -lm -l:libc.so.0 \
    -o "${DEST}/usr/lib/libmagic.so.1.0.0"
printf '\100' | dd of="${DEST}/usr/lib/libmagic.so.1.0.0" bs=1 seek=7 count=1 conv=notrunc status=none
# mirror into the cross sysroot for downstream (tdelibs)
cp -a "${DEST}"/usr/lib/libmagic.so* "${SR}/lib/"
cp "${DEST}"/usr/include/magic.h "${SR}/include/magic.h"
echo "==> libmagic staged under ${DEST}/usr"

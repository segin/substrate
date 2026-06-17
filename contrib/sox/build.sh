#!/bin/sh
# contrib/sox/build.sh — cross-build SoX for substrate.  Uses the ported
# audio codecs (libvorbis, libFLAC) for ogg/flac file support; live-audio
# backends and the codecs substrate lacks are disabled.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"; PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
TREE="${HERE}/build/sox-14.4.2"; DEST="${SUBSTRATE_TOP}/dist-overlay/dist-sox"
BINU="$(ls -d "${SUBSTRATE_TOP}"/contrib/binutils/build/binutils-*/ | head -1)"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
cd "${TREE}"
for s in config.sub config.guess; do find . -name "$s" -exec cp -f "${BINU}/$s" {} + ; done
sh "${SUBSTRATE_TOP}/contrib/substrate-libtool-shared.sh" ./configure >/dev/null 2>&1 || true
export PKG_CONFIG_PATH="${SR}/lib/pkgconfig"
DEMOTE="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types"
./configure --host=i386-unknown-substrate --prefix=/usr --enable-shared --enable-static \
    --without-ao --without-pulseaudio --without-alsa --without-oss --without-sndio --without-coreaudio --without-sunaudio \
    --without-mad --without-lame --without-twolame --without-opus --without-amrwb --without-amrnb \
    --without-png --without-ladspa --without-magic --without-id3tag --without-wavpack --without-gsm --without-lpc10 \
    --with-oggvorbis --with-flac \
    CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector ${DEMOTE}" \
    CPPFLAGS="-I${SR}/include" LDFLAGS="-L${SR}/lib"
make -j"${JOBS}"
rm -rf "${DEST}"; make install DESTDIR="${DEST}"
find "${DEST}" -name '*.la' -delete
for f in $(find "${DEST}/usr/lib" -name '*.so.*' -type f 2>/dev/null) $(find "${DEST}/usr/bin" -type f 2>/dev/null); do
    printf '\100' | dd of="${f}" bs=1 seek=7 count=1 conv=notrunc status=none
done
echo "==> sox staged under ${DEST}/usr"

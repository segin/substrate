#!/bin/sh
# contrib/fdk-aac/build.sh — cross-build libfdk-aac for substrate.
#
# fdk-aac ships both autotools and CMake.  We use CMake, as contrib/faad2
# does: it sidesteps the config.sub / libtool retrofits every autotools port
# of this era needs, and it emits the versioned shared object and the .pc
# file directly.
#
# CMAKE_SYSTEM_NAME=Linux is deliberate and is the same trick faad2 uses.
# CMake has no notion of substrate, and with an unknown system name it
# refuses to build a versioned shared library at all.  Only the CMake
# platform rules see "Linux"; every compiler variable below points at the
# substrate cross toolchain, so the objects are substrate ELF (branded 0x40
# further down).
#
# The sources are C++ (178 .cpp files) but upstream sets LINKER_LANGUAGE C:
# fdk-aac is written without the C++ runtime -- no exceptions, no RTTI, no
# operator new -- so it links with the C driver and pulls in no libstdc++.
# Keep it that way; -fno-exceptions -fno-rtti says so explicitly rather than
# relying on the code happening not to need them.
#
# --as-needed on the shared link is the other half of that.  CMake appends
# the C++ implicit link libraries whenever a target has any C++ source,
# LINKER_LANGUAGE notwithstanding, so the link line ends in "-lstdc++ -lm".
# The sysroot now has a libstdc++.so linker name resolving to the shared
# runtime, so ld recorded DT_NEEDED libstdc++.so.6 on a library with not one
# undefined C++ symbol.  --as-needed records only what is actually
# referenced, which drops stdc++ and keeps libc.
#
# Passing -DCMAKE_CXX_IMPLICIT_LINK_LIBRARIES="" does NOT work: CMake's
# compiler detection writes CMakeCXXCompiler.cmake after the cache is seeded
# and puts "stdc++" straight back.
#
# Note this is the opposite of Makefile.inc's --no-as-needed, and for the
# opposite reason: substrate's own libraries name their DT_NEEDED
# deliberately and must keep them, whereas here the trailing libraries are
# ones CMake added on its own.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

VER="2.0.3"
TREE="${HERE}/build/fdk-aac-${VER}"
DEST="${SUBSTRATE_TOP}/dist-overlay/dist-fdk-aac"

[ -d "${TREE}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# -fno-stack-protector: substrate's libc has no __stack_chk_fail_local, so
# anything the compiler protects fails to link.  The other codec ports
# (opus, flac) disable it the same way -- see substrate-codec.README.md.
CFLAGS_COMMON="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector"

cd "${TREE}"
rm -rf bld; mkdir bld; cd bld
cmake -G "Unix Makefiles" \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=i386 \
    -DCMAKE_C_COMPILER=i386-unknown-substrate-gcc \
    -DCMAKE_CXX_COMPILER=i386-unknown-substrate-g++ \
    -DCMAKE_C_FLAGS="${CFLAGS_COMMON}" \
    -DCMAKE_CXX_FLAGS="${CFLAGS_COMMON} -fno-exceptions -fno-rtti" \
    -DCMAKE_EXE_LINKER_FLAGS="-L${SR}/lib -l:libc.so.0" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--as-needed -L${SR}/lib -l:libc.so.0" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_PROGRAMS=OFF \
    -DFDK_AAC_INSTALL_PKGCONFIG_MODULE=ON \
    ..
make -j"${JOBS}"

rm -rf "${DEST}"
make install DESTDIR="${DEST}"
find "${DEST}" -name '*.la' -delete

# Stamp ELFOSABI_SUBSTRATE (0x40) at e_ident[7] on every real shared object.
# The cross ld normally does this, but a host-side relink can leave SysV (0)
# behind, and the kernel's exec dispatch reads this byte.
_n=0
for so in $(find "${DEST}/usr" -name '*.so.*.*' -type f 2>/dev/null); do
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    b=$(od -An -tx1 -j7 -N1 "${so}" | tr -d ' ')
    [ "${b}" = "40" ] || { echo "OSABI stamp failed on ${so} (got ${b})" >&2; exit 1; }
    _n=$((_n + 1))
done
echo "  OSABI->substrate on ${_n} shared objects"

# Mirror libs + headers + .pc into the cross sysroot so a consumer's
# PKG_CHECK_MODULES([AAC],[fdk-aac]) resolves and -lfdk-aac links.
mkdir -p "${SR}/lib/pkgconfig" "${SR}/include"
cp -a "${DEST}"/usr/lib/libfdk-aac.so* "${SR}/lib/" 2>/dev/null || true
cp -a "${DEST}"/usr/include/fdk-aac "${SR}/include/" 2>/dev/null || true
cp -a "${DEST}"/usr/lib/pkgconfig/fdk-aac.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true

echo "==> fdk-aac ${VER} staged under ${DEST}/usr and mirrored into ${SR}"

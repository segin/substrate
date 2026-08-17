#!/bin/sh
# contrib/taglib/build.sh — cross-build TagLib 2.0.2 (C++, CMake) for substrate.
#
# Configured with CMAKE_SYSTEM_NAME=Linux so CMake (a) treats this as a
# cross-compile (configure-time compile checks build but never execute) and
# (b) emits a real ELF SONAME'd shared library (libtag.so.2).  The compiler is
# the substrate cross g++, so the output is a substrate binary (OSABI 0x40,
# stamped below).
#
# TagLib is the one required (non-codec) dependency PsyMP3 still needs:
#   configure: PKG_CHECK_MODULES([TAGLIB],[taglib >= 1.6]) -> taglib.pc
#   sources:   #include <taglib/fileref.h> ...   link: -ltag
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"; LIB="taglib"; VERSION="2.0.2"
TREE="${HERE}/build/taglib-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
  p="${HERE}"
  while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done
  SUBSTRATE_TOP="$p"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"

[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

# No -g: the debug info roughly doubled the staged library (30 MB vs 14 MB)
# for no benefit on target.  This does NOT touch unwinding -- .eh_frame and
# .eh_frame_hdr are SHF_ALLOC sections emitted for C++ exceptions regardless
# of -g, and PT_GNU_EH_FRAME comes from the linker's --eh-frame-hdr (supplied
# by the gcc specs override).  Without that segment every throw crossing a DSO
# boundary lands in std::terminate, so it is checked after the build below.
CFLAGS="-march=i486 -mtune=i486 -O2 -fno-pie -fno-stack-protector"
# C++ shared libs: pull libstdc++/libgcc_s/libpthread/libc through and let ld
# trust the shared libs' own DT_NEEDED (substrate's `g++ -shared` adds no
# implicit libc, and CMake links libtag with --no-undefined would otherwise
# trip).  --allow-shlib-undefined keeps indirect NEEDED from failing at link.
LDFLAGS="-L${SR}/lib -Wl,-rpath-link,${SR}/lib -Wl,--allow-shlib-undefined"

rm -rf "${BS}"; mkdir -p "${BS}"
cmake -S "${TREE}" -B "${BS}" \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=i686 \
  -DCMAKE_C_COMPILER=i386-unknown-substrate-gcc \
  -DCMAKE_CXX_COMPILER=i386-unknown-substrate-g++ \
  -DCMAKE_C_FLAGS="${CFLAGS}" \
  -DCMAKE_CXX_FLAGS="${CFLAGS}" \
  -DCMAKE_EXE_LINKER_FLAGS="${LDFLAGS}" \
  -DCMAKE_SHARED_LINKER_FLAGS="${LDFLAGS}" \
  -DCMAKE_MODULE_LINKER_FLAGS="${LDFLAGS}" \
  -DCMAKE_FIND_ROOT_PATH="${SR}" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF \
  -DBUILD_EXAMPLES=OFF -DBUILD_BINDINGS=OFF

cmake --build "${BS}" -j"${JOBS}"

rm -rf "${DESTDIR}"
DESTDIR="${DESTDIR}" cmake --install "${BS}"
rm -f "${DESTDIR}"/usr/lib/*.la

# Stamp ELFOSABI_SUBSTRATE (0x40) on every produced shared object.  substrate's
# `g++ -shared` stamps ELFOSABI_SYSV (0); the kernel exec personality dispatch
# routes on this byte (file offset 7).
for so in "${DESTDIR}"/usr/lib/lib*.so.*; do
  [ -f "$so" ] || continue
  case "$so" in
    *.so.*.*)  # real file, not a symlink alias like libtag.so.2
      [ -L "$so" ] && continue
      # Drop debug info and the static symbol table.  Dropping -g from CFLAGS
      # is not sufficient on its own -- the link still lands ~5 MB of
      # .debug_* plus a .symtab in the output -- so strip explicitly and get
      # a deterministic result regardless of what upstream's CMake injects.
      #
      # --strip-unneeded is safe for a shared library: it removes .debug_*
      # and .symtab but keeps .dynsym/.dynstr/.hash (which ld.so needs to
      # resolve anything) and every SHF_ALLOC section -- including .eh_frame
      # and .eh_frame_hdr, which are allocated, not debug, and are what C++
      # unwinding runs on.  The PT_GNU_EH_FRAME assertion below proves it.
      #
      # Runs BEFORE the OSABI stamp: strip rewrites the file, so stamping
      # first would just have the byte rewritten back to 0.
      "${STAGE1_PREFIX}/bin/i386-unknown-substrate-strip" --strip-unneeded "$so"
      printf '\100' | dd of="$so" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
      b=$(od -An -tx1 -j7 -N1 "$so" | tr -d ' ')
      [ "$b" = "40" ] || { echo "OSABI stamp FAILED on $so (got $b)" >&2; exit 1; }
      echo "OSABI 0x40 OK: $so"
      # Unwind tables.  TagLib is C++ and PsyMP3 throws across the library
      # boundary, which needs PT_GNU_EH_FRAME for the unwinder to find the
      # FDEs via dl_iterate_phdr.  Without it every such throw goes straight
      # to std::terminate even with a catch(...) on the stack -- and it has
      # gone missing before, silently, when a gcc specs override swallowed
      # --eh-frame-hdr.  Fail the build rather than ship that again.
      if ! "${STAGE1_PREFIX}/bin/i386-unknown-substrate-readelf" -l "$so" \
             2>/dev/null | grep -q GNU_EH_FRAME; then
        echo "PT_GNU_EH_FRAME MISSING from $so — C++ exceptions would abort;" >&2
        echo "check the gcc specs override still passes --eh-frame-hdr" >&2
        exit 1
      fi
      echo "PT_GNU_EH_FRAME OK: $so"
      ;;
  esac
done

# Mirror libtag* + headers + taglib.pc into the cross sysroot so PsyMP3's
# PKG_CHECK_MODULES([TAGLIB],[taglib]) (taglib.pc) and #include <taglib/...>
# resolve at configure/compile/link time.
mkdir -p "${SR}/lib/pkgconfig" "${SR}/include/taglib"
cp -a "${DESTDIR}"/usr/lib/libtag.*           "${SR}/lib/"            2>/dev/null || true
cp -a "${DESTDIR}"/usr/include/taglib/.        "${SR}/include/taglib/" 2>/dev/null || true
cp -a "${DESTDIR}"/usr/lib/pkgconfig/taglib.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true

echo "==> ${LIB} ${VERSION} staged under ${DESTDIR} and mirrored into ${SR}"

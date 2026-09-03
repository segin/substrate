# Fraunhofer FDK AAC on Substrate

**fdk-aac 2.0.3**, the Fraunhofer FDK AAC codec library — encoder and
decoder.  Ported to replace `contrib/faad2` (decode-only) as PsyMP3's AAC
path.  The two can coexist: this port stages and installs under different
names, so nothing that links `-lfaad` today is disturbed.

## Source

The tarball is the `make dist` release hosted by the **opencore-amr**
project on SourceForge, which is upstream's distribution point for fdk-aac
releases.  Deliberately *not*
`github.com/mstorsjo/fdk-aac/archive/refs/tags/`: those are raw tree
snapshots with no generated `configure`, and GitHub makes no promise that
their bytes stay stable.

SHA256 `829b6b89…cb79e` was verified against Gentoo's independent Manifest
record for the same file (matching size and SHA512), not just re-downloaded
from the same mirror twice.

## Build system

Upstream ships both autotools and CMake.  This port uses **CMake**, as
`contrib/faad2` does: it avoids the `config.sub` and libtool retrofits every
autotools port of this vintage needs, and it emits the versioned shared
object and the `.pc` file directly.

`CMAKE_SYSTEM_NAME=Linux` is the same trick faad2 uses.  CMake has no notion
of substrate and will not produce a versioned shared library for an unknown
system name.  Only CMake's platform rules see "Linux"; every compiler
variable points at the substrate cross toolchain, and the result is stamped
`ELFOSABI_SUBSTRATE` (0x40) after install.

## Substrate-specific notes

No patches — the `series` file is empty.  Three build flags carry the whole
port:

- **`-fno-stack-protector`.**  substrate's libc has no
  `__stack_chk_fail_local`, so anything the compiler protects fails to link.
  The other codec ports disable it the same way; see
  `contrib/substrate-codec.README.md`.
- **`-fno-exceptions -fno-rtti`.**  The sources are C++ (178 `.cpp` files)
  but upstream sets `LINKER_LANGUAGE C`: fdk-aac is written without the C++
  runtime.  Saying so explicitly means the build fails loudly if that ever
  stops being true, rather than silently growing a libstdc++ dependency.
- **`-Wl,--as-needed` on the shared link.**  CMake appends the C++ implicit
  link libraries whenever a target has *any* C++ source, `LINKER_LANGUAGE`
  notwithstanding, so the link line ends in `-lstdc++ -lm`.  Since the cross
  sysroot now has a `libstdc++.so` linker name resolving to the shared
  runtime, ld recorded `DT_NEEDED libstdc++.so.6` on a library with not one
  undefined C++ symbol.  `--as-needed` records only what is referenced.

  Note this is the **opposite** of `Makefile.inc`'s `--no-as-needed`, and for
  the opposite reason: substrate's own libraries name their `DT_NEEDED`
  deliberately and must keep them; here the trailing libraries are ones CMake
  added on its own.

  Passing `-DCMAKE_CXX_IMPLICIT_LINK_LIBRARIES=""` does **not** work — CMake's
  compiler detection writes `CMakeCXXCompiler.cmake` after the cache is
  seeded and puts `stdc++` straight back.

## Consuming it

`build.sh` mirrors the library, the `fdk-aac/` header directory and
`fdk-aac.pc` into the cross sysroot, so a consumer needs only:

    PKG_CONFIG_LIBDIR=${SR}/lib/pkgconfig
    PKG_CHECK_MODULES([AAC], [fdk-aac])     # -> -lfdk-aac

**Do not set `PKG_CONFIG_SYSROOT_DIR` for this one.**  The `.pc` says
`prefix=/usr`, and the sysroot mirror is flat (`${SR}/lib`, `${SR}/include`)
rather than `${SR}/usr/...`, so a sysroot prefix produces
`-L${SR}/usr/lib`, which does not exist.  Unprefixed is correct here because
the cross compiler already searches `${SR}/lib` and `${SR}/include`.  That
differs from `contrib/e2tools`, which reads its `.pc` out of a
`dist-overlay/dist-*` staging tree and *does* need the sysroot prefix — the
deciding factor is which tree you point `PKG_CONFIG_LIBDIR` at.

Headers install under `usr/include/fdk-aac/`, so the include is
`<fdk-aac/aacdecoder_lib.h>` (decode) and `<fdk-aac/aacenc_lib.h>` (encode).

## Licence

FDK AAC is **not** under a standard free-software licence: it ships under the
"Software License for The Fraunhofer FDK AAC Codec Library for Android"
(`NOTICE`, `MODULE_LICENSE_FRAUNHOFER` in the tree).  It is redistributable
in source and binary form with attribution, but it is not GPL-compatible, and
it grants no patent licence — AAC is patent-encumbered in some
jurisdictions.  Distributors of a substrate image containing this library
should read `NOTICE` rather than assume the terms of the surrounding tree.

## Verifying

    readelf -h  dist-overlay/dist-fdk-aac/usr/lib/libfdk-aac.so.2.0.3
    readelf -d  dist-overlay/dist-fdk-aac/usr/lib/libfdk-aac.so.2.0.3

should report `ELF32`, `Intel 80386`, `OS/ABI: <unknown: 40>`, and

    SONAME  libfdk-aac.so.2
    NEEDED  libm.so.0
    NEEDED  libc.so.0

with **no** `libstdc++.so.6`.  The exported surface is 2680 functions
including `aacDecoder_Open` / `aacDecoder_DecodeFrame` and `aacEncOpen` /
`aacEncEncode`.

**Not yet runtime-tested on target.**  The library cross-builds, links and
exports the expected API, but nothing has decoded a stream on substrate
yet — that waits on a PsyMP3 build that consumes it.

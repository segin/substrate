# TagLib 2.3.2 — Substrate port

TagLib is an audio-metadata library (ID3v1/2, Ogg comments, FLAC, MP4, ...).
It is the one **required** (non-codec) dependency of **PsyMP3** that was still
missing from the substrate sysroot:

* PsyMP3's `configure` runs `PKG_CHECK_MODULES([TAGLIB],[taglib >= 1.6])`
  — so it needs a `taglib.pc`.
* PsyMP3 sources include `<taglib/fileref.h>`, `<taglib/tag.h>`,
  `<taglib/tiostream.h>`, `<taglib/tstring.h>` and link `-ltag`.

The host builds PsyMP3 against TagLib 2.3; we port the 2.x line because its
C++ API is what PsyMP3 expects. TagLib 2.x requires **C++17** (substrate's
`i386-unknown-substrate-g++` supports it) and builds with **CMake**.

## Layout

* `fetch.sh` — downloads + SHA-256-verifies the TagLib release tarball,
  extracts, and applies the `series` patches.
* `build.sh` — CMake cross-build into `dist-overlay/dist-taglib/`, OSABI-stamps
  the produced `.so`, and mirrors libtag + headers + `taglib.pc` into the cross
  sysroot.
* `series` — patch manifest (empty; no source fixes were needed).
* `patches/` — patch files referenced by `series` (none required).

```
./fetch.sh        # download + verify + extract
./build.sh        # cross-compile, stage, OSABI-stamp, mirror to sysroot
```

## utfcpp / utf8cpp

TagLib 2.x depends on the header-only **utfcpp** (utf8cpp) library.  The 2.0.x
release tarballs shipped an empty `3rdparty/utfcpp/` (a git submodule) and the
port fetched the pinned commit separately; since 2.1 the tarball carries it.
utfcpp's own `CMakeLists.txt` defines the `utf8::cpp` INTERFACE target that
TagLib's `add_subdirectory("3rdparty/utfcpp")` fallback consumes, so the build
is offline with no `git submodule` step.  `fetch.sh` fails if a tarball ever
arrives without it.

## CMake cross configuration

* `CMAKE_SYSTEM_NAME=Linux` flips CMake into cross-compile mode (configure-time
  compile/link probes are built but never executed) **and** makes it emit a
  real SONAME'd ELF shared library (`libtag.so.2`) instead of a host-style one.
* `CMAKE_{C,CXX}_COMPILER` = the substrate cross gcc/g++; `-march=i486
  -mtune=i486 -O2 -g -fno-pie -fno-stack-protector`.
* `CMAKE_FIND_ROOT_PATH = $SR` (`/opt/substrate/i386-unknown-substrate`) with
  LIBRARY/INCLUDE/PACKAGE rooted ONLY there — so the build finds substrate's
  `zlib` (already in the sysroot) and never the host's.
* `BUILD_SHARED_LIBS=ON`, `BUILD_TESTING=OFF`, `BUILD_EXAMPLES=OFF`,
  `BUILD_BINDINGS=OFF`.

`WITH_ZLIB` is left ON (default); substrate's `libz.so.1` is in the sysroot, so
TagLib links it for compressed-ID3 support and `taglib.pc` carries `-lz`.

## C++ shared-library linkage

substrate's `g++ -shared` adds no implicit libc, so the link flags pull the
C++ runtime through explicitly and trust the shared libs' own DT_NEEDED:

```
-L$SR/lib -Wl,-rpath-link,$SR/lib -Wl,--allow-shlib-undefined
```

substrate's `libstdc++.so.6` already carries `libpthread.so.0` in its
DT_NEEDED, so no explicit `-lpthread` was needed. The resulting
`libtag.so.2` NEEDs `libz.so.1`, `libstdc++.so.6`, `libm.so.0`,
`libc.so.0`.

## OSABI

`g++ -shared` stamps `ELFOSABI_SYSV` (0). `build.sh` rewrites byte 7 of every
produced `lib*.so.*.*` to `0x40` (`ELFOSABI_SUBSTRATE`) and verifies it
(`od -An -tx1 -j7 -N1` == `40`) — this is the byte the kernel exec personality
dispatch routes on.

## Output

* `dist-overlay/dist-taglib/usr/lib/libtag.so.2.3.2`,
  with `libtag.so.2` and `libtag.so` symlinks. (Shared-only — no `libtag.a`,
  since `BUILD_SHARED_LIBS=ON`.)
* `dist-overlay/dist-taglib/usr/include/taglib/*.h`
* `dist-overlay/dist-taglib/usr/lib/pkgconfig/taglib.pc`
* mirrored into `$SR/lib`, `$SR/include/taglib`, `$SR/lib/pkgconfig` so PsyMP3's
  `PKG_CHECK_MODULES([TAGLIB],[taglib])` and `#include <taglib/...>` resolve.

## Upstream

* TagLib: <https://taglib.org/> — release
  `https://github.com/taglib/taglib/releases/download/v2.3.2/taglib-2.3.2.tar.gz`
  (fallback `https://taglib.github.io/releases/taglib-2.3.2.tar.gz`),
  SHA-256 `3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc`.
* utfcpp: <https://github.com/nemtrif/utfcpp>, bundled in the tarball.

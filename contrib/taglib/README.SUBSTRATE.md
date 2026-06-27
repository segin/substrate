# TagLib 2.0.2 — Substrate port

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

* `fetch.sh` — downloads + SHA-256-verifies the TagLib release tarball **and**
  the pinned `utfcpp` submodule, extracts, and applies the `series` patches.
* `build.sh` — CMake cross-build into `dist-overlay/dist-taglib/`, OSABI-stamps
  the produced `.so`, and mirrors libtag + headers + `taglib.pc` into the cross
  sysroot.
* `series` — patch manifest (empty; no source fixes were needed).
* `patches/` — patch files referenced by `series` (none required).

```
./fetch.sh        # download + verify + extract + populate 3rdparty/utfcpp
./build.sh        # cross-compile, stage, OSABI-stamp, mirror to sysroot
```

## utfcpp / utf8cpp (the one real gotcha)

TagLib 2.x depends on the header-only **utfcpp** (utf8cpp) library. The release
tarball ships an **empty** `3rdparty/utfcpp/` directory — it is a git
submodule, **not** vendored into the tarball. With nothing there, CMake's
config step hits

```
CMake Error: utfcpp not found. Either install package ... or fetch the git
submodule using `git submodule update --init`
```

TagLib 2.0.2 pins utfcpp at commit `df857efc5bbc2aa84012d865f7d7e9cccdc08562`
(see the tarball's `.gitmodules` + the tree at tag `v2.0.2`). `fetch.sh`
downloads that exact commit's tarball (SHA-256 verified) and drops it into
`3rdparty/utfcpp/`. utfcpp ships its own `CMakeLists.txt` defining the
`utf8::cpp` INTERFACE target, which is exactly what TagLib's
`add_subdirectory("3rdparty/utfcpp")` fallback path consumes — so the build is
fully offline and reproducible with no `git submodule` step.

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
`libtag.so.2.0.2` NEEDs `libz.so.1`, `libstdc++.so.6`, `libm.so.0`,
`libc.so.0`.

## OSABI

`g++ -shared` stamps `ELFOSABI_SYSV` (0). `build.sh` rewrites byte 7 of every
produced `lib*.so.*.*` to `0x40` (`ELFOSABI_SUBSTRATE`) and verifies it
(`od -An -tx1 -j7 -N1` == `40`) — this is the byte the kernel exec personality
dispatch routes on.

## Output

* `dist-overlay/dist-taglib/usr/lib/libtag.so.2.0.2` (~22 MiB, `-g`),
  with `libtag.so.2` and `libtag.so` symlinks. (Shared-only — no `libtag.a`,
  since `BUILD_SHARED_LIBS=ON`.)
* `dist-overlay/dist-taglib/usr/include/taglib/*.h`
* `dist-overlay/dist-taglib/usr/lib/pkgconfig/taglib.pc`
* mirrored into `$SR/lib`, `$SR/include/taglib`, `$SR/lib/pkgconfig` so PsyMP3's
  `PKG_CHECK_MODULES([TAGLIB],[taglib])` and `#include <taglib/...>` resolve.

## Upstream

* TagLib: <https://taglib.org/> — release
  `https://taglib.github.io/releases/taglib-2.0.2.tar.gz`
  (fallback `https://github.com/taglib/taglib/releases/download/v2.0.2/taglib-2.0.2.tar.gz`),
  SHA-256 `0de288d7fe34ba133199fd8512f19cc1100196826eafcb67a33b224ec3a59737`.
* utfcpp: <https://github.com/nemtrif/utfcpp> @
  `df857efc5bbc2aa84012d865f7d7e9cccdc08562`, SHA-256
  `911ff4f13cc7bfece2b5f65e7468b962db19c9727f89d560b2617360af08f538`.

# faad2 on Substrate

Cross-build of **faad2 2.11.1** (libfaad — the Freeware Advanced Audio Decoder,
an AAC/MP4 audio decoder).  Consumed by PsyMP3 via
`PKG_CHECK_MODULES([AAC],[faad2])`, which links `-lfaad` and includes
`neaacdec.h`.

## Layout
- `fetch.sh` — pins the upstream GitHub release tarball
  (`faad2-2.11.1.tar.gz`) by SHA-256, extracts into `build/`, and applies the
  patch series (currently empty — upstream builds unmodified).
- `build.sh` — cross-builds and stages into
  `dist-overlay/dist-faad2/usr/`, then mirrors the libraries, headers, and the
  pkg-config file into the cross sysroot at
  `/opt/substrate/i386-unknown-substrate/{lib,include,lib/pkgconfig}`.
- `patches/` + `series` — source fixes as a reproducible quilt-style series
  (empty for this port).

## Build approach
faad2 2.11.1 is **CMake-only** (no autotools `configure`).  It is configured
with `CMAKE_SYSTEM_NAME=Linux` so CMake emits a normal versioned ELF shared
library (`libfaad.so.2.11.1` with the `libfaad.so.2` / `libfaad.so` symlinks),
while `CMAKE_C_COMPILER=i386-unknown-substrate-gcc` keeps the output substrate
ELF.  Flags: `-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector`;
the executable/shared linker flags add `-L$SR/lib -l:libc.so.0`.

After install, every real `lib*.so.*.*` is stamped with
`ELFOSABI_SUBSTRATE` (0x40) at `e_ident[7]` and the stamp is verified
(`od -An -tx1 -j7 -N1` must report `40`), because the host `cmake`/`gcc -shared`
path otherwise leaves the OSABI byte at SysV (0).

## Artifacts
- `libfaad.so.2.11.1` (+ `.so.2`, `.so` symlinks) — the AAC decoder PsyMP3 needs.
- `libfaad_drm.so.2.11.1` (+ symlinks) — the DRM (Digital Radio Mondiale) variant.
- `usr/bin/faad` — the reference CLI decoder.
- `usr/include/{neaacdec.h,faad.h}` — public API headers.
- `usr/lib/pkgconfig/faad2.pc` — emitted by upstream CMake; `Name: FAAD2`,
  `Libs: -lfaad`, so PsyMP3's `PKG_CHECK_MODULES([AAC],[faad2])` resolves.

CMake's `BUILD_SHARED_LIBS=ON` produces shared libraries only (no `libfaad.a`);
PsyMP3 links `-lfaad` dynamically.

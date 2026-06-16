# Trinity Desktop Environment (TDE) — substrate port

TDE is a maintained fork of KDE 3.5, built on its own fork of Qt 3
(**TQt3**).  Porting it is a large, multi-stage effort: the desktop sits
on top of a deep dependency chain, each layer a substantial cross-compile
of its own.  This directory is laid out as one sub-port per layer, in
dependency order, following the standard `contrib/` convention
(`fetch.sh` + `build.sh` + `patches/` + `series`; nothing under `build/`
is vendored).

Sources are the TDE **R14.1.6** release tarballs from
`mirror.ppa.trinitydesktop.org/trinity/releases/R14.1.6/main/`.

## Dependency chain (build order)

| # | sub-port      | what it is                                   | build system |
|---|---------------|----------------------------------------------|--------------|
| 1 | **tqt3**      | the Qt 3 fork; everything depends on it      | Qt3 configure + qmake |
| 2 | tqtinterface  | TQt↔Qt compatibility shim                    | CMake |
| 3 | arts (opt.)   | aRts sound server — stub/skip on substrate   | CMake |
| 4 | **tdelibs**   | core libraries (tdecore, tdeui, tdeio, …)    | CMake |
| 5 | **tdebase**   | the desktop proper (twin, kicker, konqueror…)| CMake |
|   | …             | tdeutils, tdegraphics, … (optional apps)     | CMake |

Layers 2–5 are CMake projects and reuse the substrate CMake cross
toolchain already used by the ctwm/CDE ports
(`contrib/ctwm/substrate-toolchain.cmake`), pointed at a sysroot that
includes the freshly built TQt3.

## Prerequisites — already ported (`contrib/`)

The base needed by TQt3/tdelibs is in place: the X11 client stack
(libX11, libXext, libXrender, libICE, libSM, libXmu, libXt, …),
freetype, fontconfig, libpng, libjpeg, zlib, openssl.  The substrate
cross toolchain (`/opt/substrate`, `i386-unknown-substrate-gcc`) and its
sysroot already carry the X11 headers/libs.

## Status

**Stage 1 (TQt3): in progress — configures and cross-compiles.**

- `tqt3/fetch.sh` downloads + verifies (SHA512) + extracts TQt3 and
  installs the substrate pieces.
- A **substrate-g++ mkspec** (`tqt3/substrate-g++/`) drives the cross
  compile: `i386-unknown-substrate-g++`, `-march=i486`, X11 from the
  cross sysroot.
- `tqt3/build.sh` runs `configure -platform linux-g++ -xplatform
  substrate-g++ …` (host qmake/moc built with the host gcc, libraries
  cross-built) — **configure succeeds** and `make` compiles real TQt3
  sources.
- Patch `0001-ntqglobal-substrate-os.patch` teaches TQt's OS detection
  about substrate (`__substrate__` → `Q_OS_LINUX`; substrate is
  POSIX/ELF and Linux-like for Qt's purposes).  With it the build gets
  past the `"TQt has not been ported to this OS"` `#error` and into the
  X11 widget code.

**Current blocker:** TQt3 pulls in X11 extension headers that substrate's
X stack doesn't ship — `X11/extensions/Xrandr.h` first, then
Xinerama/Xcursor/Xft.  `build.sh` already passes
`-no-xrandr -no-xinerama -no-xcursor -no-xft` to configure; the next step
is to verify those reach every `.pro` (the widget code includes some
extension headers unconditionally) and either gate them or stage the
extension headers/libs into the sysroot.

### Next steps
1. Finish TQt3: clear the X-extension includes, then iterate `make` —
   expect a tail of substrate-libc/POSIX deltas (each a small patch).
2. Stage TQt3 into the cross sysroot and add the `tqtinterface` CMake
   sub-port.
3. `tdelibs`, then `tdebase` (twin + kicker + tdeinit) for a minimal
   live desktop — the same milestone the CDE port reached.

This is a long road; it is checked in incrementally so each layer's work
is reproducible from clean sources.

## Building

```sh
cd contrib/tde/tqt3
./fetch.sh        # download + verify + extract + patch
./build.sh        # cross-compile (work in progress)
```

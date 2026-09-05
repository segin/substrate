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

| # | sub-port      | what it is                                   | build system | status |
|---|---------------|----------------------------------------------|--------------|--------|
| 0 | tde-cmake     | shared TDE CMake modules (TDEMacros, FindTQt)| (modules)    | ✅ fetched |
| 1 | **tqt3**      | the Qt 3 fork; everything depends on it      | Qt3 configure + qmake | ✅ built + staged |
| 2 | **tqtinterface** | TQt↔Qt compatibility shim                 | CMake        | ✅ built + staged |
| 3 | **dbus-1-tqt**| TQt D-Bus binding; a tdelibs dependency      | CMake        | ✅ built + staged |
| 4 | **tdelibs**   | core libraries (tdecore, tdeui, tdeio, dcop) | CMake        | ✅ built + staged |
| 5 | **tdebase**   | the desktop proper (twin, kicker, tdeinit…)  | CMake        | ✅ built + staged |
| 6 | tdeutils / tdegames / tdetoys | optional application sets    | CMake        | ✅ built + staged |
|   | arts (opt.)   | aRts sound server — stub/skip on substrate   | CMake        | not attempted |

The CMake layers (2+) cross-build via the shared toolchain
`contrib/tde/substrate-tde-toolchain.cmake` (C+C++, sysroot
`/opt/substrate/i386-unknown-substrate`) plus the `tde-cmake` modules on
`CMAKE_MODULE_PATH`.  The toolchain force-links `-lpthread`: substrate
keeps `pthread_*` in libpthread (not libc) and the substrate
`libstdc++.so` does not carry libpthread in its `DT_NEEDED`, so any
C++ link that pulls libstdc++ (every TDE target) must add it explicitly.

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

**Stage 1 (TQt3): DONE — cross-builds, links, and stages.**

`tqt3/build.sh` produces `libtqt-mt.so.3.5.0` (substrate i386, ELF
OSABI 64), the JPEG image-format plugin, the four input-method plugins,
397 public headers, and the host build tools (tquic/tqmoc/tqmake),
staged under `dist-tqt3/opt/trinity/` (~156 MiB).

Build mechanics:
- `tqt3/fetch.sh` downloads + verifies (SHA512) + extracts TQt3 and
  applies the patch series.
- A **substrate-g++ mkspec** (`tqt3/substrate-g++/`) drives the cross
  compile: `i386-unknown-substrate-g++`, `-march=i486`, X11 + Xft from
  the cross sysroot.  It also force-defines `TQT_NO_XRANDR`/`XINERAMA`/
  `XCURSOR`/`XKB` so *every* unit (plugins/tools, not just `src/`) skips
  the X-extension headers we configure off.
- `tqt3/build.sh` runs `configure -platform linux-g++ -xplatform
  substrate-g++ …` then builds only the library targets
  (`src-qmake src-moc sub-src sub-plugins`); the GUI `tools/`,
  `tutorial/`, `examples/` are skipped (TDE needs only the library +
  headers + host moc/uic/qmake).

Patches:
- `0001-ntqglobal-substrate-os.patch` — `__substrate__` → `Q_OS_LINUX`
  in both `include/` and `src/tools/` copies of `ntqglobal.h` (gets
  past the `"not been ported to this OS"` `#error`).
- `0002-qjpegio-libjpeg-enum-boolean.patch` — substrate's IJG libjpeg9
  defines `boolean` as an *enum*, so the libjpeg callbacks use `TRUE`
  rather than C++ `true`.
- `0003-qdns-no-libc-resolver-on-substrate.patch` — substrate libc has
  no BSD resolver (`res_init`/`__res_state`); `TQDns::doResInit()`
  relies on its `/etc/resolv.conf` parse instead.

threading needed real thread-cancellation entry points, added to
substrate's libpthread (`pthread_cancel`/`testcancel`/`setcancelstate`/
`setcanceltype`).  Antialiased fonts needed a `libXft` port
(`contrib/libXft/`), staged into the cross sysroot, built `-xft`.

**The host `tquic` (uic).** configure auto-host-builds only `*moc*`
targets, and uic links the full `-ltqt-mt` (so the standalone-moc trick
won't work).  `build.sh` therefore does a small **native host TQt3
build** in `hostbuild/` to get a runnable host `tquic`, makes it
relocatable (`chrpath -r '$ORIGIN'` with `libtqt-mt.so.3` beside it),
and drops it into the cross tree's `bin/` (cross configure already
points `QMAKE_UIC` there) and into the staged `/opt/trinity/bin`.

**Stage 2 (tqtinterface): DONE — cross-builds with no patches.**
`tqtinterface/build.sh` configures against the cross TQt3
(`QT_PREFIX_DIR` → the dist-tqt3 stage), uses TQt3's host tqmoc/tquic,
and produces `libtqt.so.4.2.0` (substrate i386, OSABI 64) + 335 tq*.h
compat headers + `tqt.pc`, staged under
`dist-overlay/dist-tqtinterface/opt/trinity`.  The CMake configure's
own C++ test compiles + links against `-ltqt-mt` cleanly.

### Next steps
1. **Live-boot verification.**  The stack builds and stages; what has not
   been re-confirmed since the fork/no-fork workaround cluster was dropped
   is a desktop that comes up on target.  That is the milestone the CDE
   port reached.
2. **arts** — the sound server, still unattempted; TDE runs without it.
3. **CI.**  TDE is not in `build.sh`'s `DEFAULT_CONTRIB`.  It is by far the
   largest thing in the tree (~620 MB staged, TQt3 another ~219 MB), and
   the bootstrap it would join has not yet gone green on the smaller set.

## Building

The whole stack, in dependency order:

```sh
contrib/tde/fetch.sh      # every sub-port's fetch.sh
contrib/tde/build.sh      # every sub-port's build.sh, merging between layers
```

Those two exist so TDE presents as a single port: `build.sh`'s contrib loop
drives `contrib/<pkg>/fetch.sh` and `contrib/<pkg>/build.sh`, and TDE's real
work lives a directory deeper.  A single sub-port still builds on its own:

```sh
cd contrib/tde/tqt3
./fetch.sh        # download + verify + extract + patch
./build.sh        # native host tquic + cross-build lib/plugins -> dist-tqt3/
```

`tqt3/build.sh` needs `chrpath` on the build host (to make the host `tquic`
relocatable).  The first run does a one-time native host build under
`hostbuild/` (cached on re-runs).  The CMake layers need a **host** cmake;
`contrib/cmake` is a different thing — it cross-builds cmake to run *on*
substrate, and is not what builds TDE.

`merge-staging.sh` assembles the merged sysroot each CMake layer compiles
against.  Its header says it is run before every sub-port; in practice only
dbus-1-tqt called it, so `contrib/tde/build.sh` now runs it between layers,
which is what makes the order reproducible from clean.

# tdebase (TDE Stage 5) — substrate cross-port

The Trinity Desktop Environment base: the window manager (twin), panel
(kicker), desktop (kdesktop), file manager / browser (konqueror),
terminal (konsole), display manager (tdm), control center (kcontrol),
session manager (ksmserver), klipper, and the rest of the core desktop.

## Build

    ./fetch.sh            # download + verify + apply patch series
    ./build.sh            # cross-build, install into dist-overlay/dist-tdebase,
                          # OSABI-stamp, merge into dist-overlay/dist-tde-sysroot

Prerequisites (build first): `contrib/tde/{tqt3,tqtinterface,tde-cmake,
dbus-1-tqt,tdelibs}`, the X11 / freetype / fontconfig / jpeg / png stack,
`libXi` / `libXfixes` / `libXtst`, and the **host** TQt3 + tdelibs tool
builds (`contrib/tde/tqt3` host tools and `contrib/tde/tdelibs/hostbuild.sh`,
which produce `dcopidl2cpp`, `tdeconfig_compiler`, `maketdewidgets`,
`tde-config`, and a `meinproc` stub).  `build.sh` additionally compiles the
`gen` binding generator natively (it uses only TQt).

## Host code generators

TDE runs several freshly-built programs ON THE BUILD HOST during the
build.  A cross-built (substrate) binary cannot execute there, so the
build points cmake at native ones via cache variables:
`KDE3_DCOPIDL2CPP_EXECUTABLE`, `KDE3_KCFGC_EXECUTABLE`,
`KDE3_MAKETDEWIDGETS_EXECUTABLE`, `KDECONFIG_EXECUTABLE` (tde-config),
`KDE3_MEINPROC_EXECUTABLE` (a stub — docs are disabled), `GEN_EXECUTABLE`.

`tde-config` is built with its install prefix pointing at the staged
cross sysroot so `tde-config --install …` returns the real on-disk paths
that the cross configure needs for `-I`/`-L`.

## Disabled / gated components

- **docs** (`BUILD_DOC=OFF`): handbooks need a runnable meinproc + the
  docbook XSL tree.
- **tsak / tdekbdledsync** (`-DBUILD_TSAK=OFF -DBUILD_TDEKBDLEDSYNC=OFF`):
  require libudev, which substrate lacks.
- **WITH_XKB_TRANSLATIONS=OFF**: needs the xkeyboard-config locale tree.
- **nfs ioslave** (`WITH_NFS`, patch 0001): needs a host rpcgen.
- **joystick KCM** (patch 0002): needs `linux/joystick.h`.
- **konsole bundled fonts** (patch 0002): need bdftopcf (substrate ships
  BDF directly); konsole uses system X fonts.
- **keramik twin client** (patch 0002): embeds tiles via genembed.

## Patches

- `0001` make the nfs ioslave optional (no host rpcgen).
- `0002` gate joystick / konsole-fonts / keramik twin client.
- `0003` tdm: link X11/Xau/dbus explicitly (tde_add_executable dropped
  the LINK list) and treat crypt() as living in libc (no libcrypt).
- `0004` source portability: drop libkonq's stray enum-declared global
  (multiple-definition under -fno-common); add `<pthread.h>` to the
  kdesktop lock screen.
- `0005` allow a host-built `gen` binding generator via `GEN_EXECUTABLE`.

## Toolchain notes (in `contrib/tde/substrate-tde-toolchain.cmake`)

- `-fcommon`: TDE 14.x has tentative-style globals in headers.
- `-l:libregex.so.0`: substrate keeps POSIX regex in libregex, not libc.
- staged-sysroot `-rpath-link`: resolves indirect DT_NEEDED of the
  cross-built TQt/TDE libs.
- `--allow-shlib-undefined`: the `tdeinit_*` wrappers NEED build-tree
  `.so` not on the link path; the DT_NEEDED is correct, so ld.so
  resolves them at runtime.

# libXScrnSaver (libXss) on Substrate

The X Screen Saver client extension library — `libXss.so.1` and
`<X11/extensions/scrnsaver.h>`.  CDE's `dtsession` drives the screen
saver through it (`XScreenSaverQueryInfo`, `XScreenSaverRegister`,
`XScreenSaverSelectInput`, `XScreenSaverSetAttributes`, ...); its
`Makefile.am` already lists `-lXss` on the Linux link line.

## Layout
- `fetch.sh` — download libXScrnSaver 1.2.4 (SHA-256 verified) + extract.
- `build.sh` — cross-configure/build/stage into `dist-libXScrnSaver/`.

No substrate source patches are required:
- the saver protocol headers (`X11/extensions/saver.h`,
  `saverproto.h`) already ship from `contrib/xorgproto`;
- `config.sub` learns `substrate*`, and libtool's generated
  `configure` is taught to treat `substrate*` like `linux*` so
  `--enable-shared` actually emits the `.so` (same one-line sed the
  other X library ports use);
- the produced `libXss.so.1` gets its ELF OSABI byte stamped to
  `ELFOSABI_SUBSTRATE` (0x40) by the `dd` post-step.

## Build
```sh
./fetch.sh
./build.sh
```
Depends on the already-built X client stack (`xorgproto`, `libX11`,
`libXext`) in their `dist-*` trees.

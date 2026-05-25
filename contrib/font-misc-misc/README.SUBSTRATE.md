# font-misc-misc

X.Org "misc-fixed" bitmap font family — `fixed` (6x13), `9x15`, `10x20`,
the cursor font, the legacy 75dpi/100dpi base, etc.  This is the most
fundamental X font package; without it, every legacy client (xterm,
xclock, twm, matwm2, ...) trips over `XOpenFont` returning NULL on its
default request and the X server typically crashes one client request
later.

## Build

```sh
./fetch.sh
./build.sh
```

Result: `dist-font-misc-misc/usr/share/fonts/X11/misc/` with each `.bdf`
source file and a hand-written `fonts.dir` index.

## Why BDF, not PCF

The canonical upstream build runs `bdftopcf` to produce binary PCFs,
which load faster.  substrate doesn't yet ship `bdftopcf` and the
host tooling here doesn't either, so we install the BDFs verbatim.
libXfont's bitmap module reads BDF directly — slower than PCF for huge
fonts, but irrelevant for the ~30 small bitmap files in this package.

## Install

`build-rootfs.sh` picks the staged tree up automatically (it iterates
every `dist-*` directory).  After a rebuild, the fonts appear at
`/usr/share/fonts/X11/misc/` on the booted image.

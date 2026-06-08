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
source file, the derived ISO8859-1 variants (see below), a generated
`fonts.dir` index and a `fonts.alias` for the short names (`fixed`, `9x15`,
…).

## ISO8859-1 derivation (the dtwm/twm title tofu fix)

The upstream BDFs are ISO10646-1 (2-byte) only.  Fontset clients (twm,
dtwm/CDE window titles) under `LANG=en_US.UTF-8` bind their Latin slots
to a **single-byte ISO8859-1** font; with only the 2-byte master present,
libX11 drops it into the 1-byte slot and `XmbDrawString` mis-packs ASCII
into `.notdef` tofu boxes.  `build.sh` therefore derives an ISO8859-1
copy of every ISO10646-1 master (row 0 of ISO10646-1; same recipe as the
`font-adobe-{75,100}dpi` ports) and points `fonts.alias`' `fixed` at it.
Verified: `XCreateFontSet("fixed")` then loads
`-Misc-Fixed-…-ISO8859-1 (1-byte)` instead of the 2-byte master.

> Historical note: an earlier revision shipped no `fonts.dir`, on the
> belief that substrate's libXfont BDF reader crashed on these fonts
> (`bdfReadBitmap:162`).  That is no longer true — tested against pristine
> font data the reader loads them cleanly; the crashes seen at the time
> were debugfs inode-aliasing corruption of hand-injected files, not the
> parser.  The `fonts.dir` is shipped again.

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

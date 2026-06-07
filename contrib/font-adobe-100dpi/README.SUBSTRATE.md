# font-adobe-100dpi

X.Org Adobe 100dpi bitmap font family — `-adobe-helvetica-*`,
`-adobe-times-*`, `-adobe-courier-*`, `-adobe-new century schoolbook-*`,
and `-adobe-symbol-*` at 8/10/12/14/18/24 point, rendered for 100dpi
displays.

This is the 100dpi companion to `font-adobe-75dpi`.  Classic X clients
(twm in particular) request `-adobe-helvetica-bold-r-normal--*-120-*`
for menu/title text; depending on the X server's resolution the match
resolves into the 75dpi or 100dpi pixel size, so both packages are
installed.  Without them twm renders text as tofu boxes under the
session's UTF-8 locale (see `font-adobe-75dpi`).

## Build

```sh
./fetch.sh
./build.sh
```

Result: `dist-font-adobe-100dpi/usr/share/fonts/X11/100dpi/` with each
`.bdf` source file and a generated `fonts.dir` index.

## Why BDF, not PCF

The upstream build runs `bdftopcf` to produce binary PCFs; substrate
doesn't ship `bdftopcf` and the host tooling here doesn't either, so the
BDFs are installed verbatim.  libXfont's bitmap module reads BDF
directly.  The 1.0.4 release ships **ISO10646-1** (Unicode) BDFs.

## Install

`build-rootfs.sh` picks the staged tree up automatically (it iterates
every `dist-*` directory).  The X server's compiled default font path
already lists `/usr/share/fonts/X11/100dpi/`, so the directory is
honored as soon as it contains a `fonts.dir` — no font-path change
needed.

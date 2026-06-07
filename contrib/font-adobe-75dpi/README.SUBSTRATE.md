# font-adobe-75dpi

X.Org Adobe 75dpi bitmap font family — `-adobe-helvetica-*`,
`-adobe-times-*`, `-adobe-courier-*`, `-adobe-new century schoolbook-*`,
and `-adobe-symbol-*` at 8/10/12/14/18/24 point.

These are the fonts classic X clients request *by default*.  In
particular twm's compiled-in `system.twmrc` asks for
`-adobe-helvetica-bold-r-normal--*-120-*` for its title, menu, resize,
and icon-manager text.  With no adobe-helvetica installed, twm — running
under the session's `LANG=en_US.UTF-8` locale, where it uses an
`XFontSet` — gets an empty/uncovered fontset and `XmbDrawString` paints
every glyph as a `.notdef` tofu box.  Installing this package (and
`font-adobe-100dpi`) makes twm render real text.

## Build

```sh
./fetch.sh
./build.sh
```

Result: `dist-font-adobe-75dpi/usr/share/fonts/X11/75dpi/` with each
`.bdf` source file and a generated `fonts.dir` index.

## Why BDF, not PCF

The upstream build runs `bdftopcf` to produce binary PCFs; substrate
doesn't ship `bdftopcf` and the host tooling here doesn't either, so the
BDFs are installed verbatim.  libXfont's bitmap module reads BDF
directly.  The 1.0.4 release ships **ISO10646-1** (Unicode) BDFs, so the
fonts slot straight into a UTF-8 fontset with full Latin coverage.

## Install

`build-rootfs.sh` picks the staged tree up automatically (it iterates
every `dist-*` directory).  The X server's compiled default font path
already lists `/usr/share/fonts/X11/75dpi/`, so the directory is honored
as soon as it contains a `fonts.dir` — no font-path change needed.

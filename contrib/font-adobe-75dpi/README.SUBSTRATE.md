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
directly.  The 1.0.4 release ships **ISO10646-1** (Unicode) BDFs.

## ISO8859-1 variants (required, not optional)

`build.sh` also derives an **ISO8859-1** (single-byte) copy of every
ISO10646-1 master and ships both.  This is mandatory, not a nicety: the
X11 `en_US.UTF-8` locale's `XLC_FONTSET` binds its Latin/ASCII slots
(`fs0 = ISO8859-1:GL`, `fs1 = ISO8859-1:GR`) to ISO8859-1 *single-byte*
fonts.  With only the 2-byte ISO10646-1 fonts present, libX11 drops a
2-byte font into a 1-byte slot; the `is_xchar2b` flag then disagrees
with the converter's single-byte output and `XmbDrawString` pairs the
bytes into bogus `XChar2b` indices — every two characters render as one
`.notdef` tofu box (the "twm font bug").  ISO8859-1 is row 0 of
ISO10646-1, so the master already holds every glyph; the derivation just
keeps the `ENCODING 0..255` subset and relabels the registry.  Upstream
gets these for free from `bdftopcf` + fontenc recoding; we synthesize
them in `build.sh` instead.

## Install

`build-rootfs.sh` picks the staged tree up automatically (it iterates
every `dist-*` directory).  The X server's compiled default font path
already lists `/usr/share/fonts/X11/75dpi/`, so the directory is honored
as soon as it contains a `fonts.dir` — no font-path change needed.

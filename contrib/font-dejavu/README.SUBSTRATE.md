# DejaVu 2.37 — substrate port

DejaVu Sans, Serif and Sans Mono (with condensed, bold, oblique and
ExtraLight faces, plus DejaVu Math TeX Gyre): 22 TrueType fonts with broad
Latin, Greek, Cyrillic, Arabic, Hebrew and symbol coverage.  Licence:
Bitstream Vera derivative (free; see `/usr/share/doc/font-dejavu/LICENSE`).

## Build

```sh
./fetch.sh        # SHA-256 matches FreeBSD's x11-fonts/dejavu distinfo
./build.sh        # -> dist-font-dejavu/usr/share/fonts/dejavu/*.ttf
```

No compile step and no dependencies: the release asset ships the `.ttf`
files.

## What it installs

- `/usr/share/fonts/dejavu/*.ttf`, which fontconfig's
  `<dir>/usr/share/fonts</dir>` already covers.
- DejaVu's own fontconfig rules into `/usr/share/fontconfig/conf.avail/`,
  enabled by symlinks in `/etc/fonts/conf.d/` (the layout contrib/fontconfig
  uses): `57-dejavu-*.conf` describe the families and
  `20-unhint-small-dejavu-*.conf` turn hinting off at small sizes.
- The licence, authors and README under `/usr/share/doc/font-dejavu/`.

fontconfig's stock `60-latin.conf` lists DejaVu first for `sans-serif`,
`serif` and `monospace`, so Xft, cairo and Pango clients (Tk, GTK 2, TDE,
xclock, ...) use it with no further configuration.  The fontconfig cache is
built at boot by `/etc/rc.d/03-fc-cache`.

# libXfont2 2.0.7 — substrate port

X font handling library used by the X server's font path.

## Substrate-specific

* Built with `--disable-freetype` to skip the freetype/libpng dep
  chain.  Bitmap-only fonts (BDF / PCF) work; TrueType won't —
  which is fine for kdrive Xfbdev with the X core fonts only.
* Required OPEN_MAX in <limits.h> (previously missing) — see the
  related commit that touched include/limits.h.  fslibos.h needs
  it to size internal font-file FD tables.

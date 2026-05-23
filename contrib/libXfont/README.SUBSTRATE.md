# libXfont 1.5.4 — substrate port

X font library (legacy v1 API).  xorg-server <= 1.16 links against
this; >= 1.19 uses libXfont2.  Kept around for the Xfbdev resurrection
path (porting the 1.16-era kdrive/fbdev/ backend into the 1.20+ tree
still needs the v1 API for some of the font hooks).

Built `--disable-freetype` to avoid pulling in freetype/libpng/harfbuzz.
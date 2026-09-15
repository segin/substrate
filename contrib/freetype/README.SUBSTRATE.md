# freetype (2.13.2)

Font rasterizer for cairo/pango/fontconfig.  Cross-built against dist-zlib +
dist-libpng via the substrate-autotools helpers; --without-brotli/bzip2.
Produces libfreetype.so + freetype2.pc.

Built twice.  This port is the first pass, --without-harfbuzz, early in
DEFAULT_CONTRIB because harfbuzz itself is built --with-freetype.
contrib/freetype-harfbuzz, right after harfbuzz, reruns this build.sh with
FREETYPE_WITH_HARFBUZZ=1 and replaces dist-freetype with a libfreetype.so.6
that links libharfbuzz.so.0.  Ports built in between link the first pass,
which has the same soname and symbols; the image carries the second.

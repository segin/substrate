# fontconfig (2.14.2)

Font configuration + matching for cairo/pango.  Cross-built against
dist-freetype, dist-expat, dist-libpng (freetype was built --with-png, so the
build-time fc-cache pulls libpng transitively).  --disable-docs;
default font dir /usr/share/fonts; config under /etc/fonts.  Produces
libfontconfig.so + fontconfig.pc.

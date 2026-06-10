# gtk2 (GTK+ 2.24.33 — last GTK+ 2.x)

The GTK+ 2 toolkit.  Cross-built via substrate_sysroot against the full
stack: glib2, gdk-pixbuf, pango (+harfbuzz/fribidi), atk, cairo (pdf
backend), fontconfig/freetype/libpng/pixman and the X client libraries
(libX11/libXext/libXrender).  Produces libgtk-x11-2.0, libgdk-x11-2.0,
libgailutil + gtk+-2.0.pc.

Build notes:
- -std=gnu11 + a -Wno-error set: GTK 2.24 predates gcc16's promotion of
  incompatible-pointer-types/int-conversion/etc. to hard errors.
- SRC_SUBDIRS trimmed to `gdk gtk modules`: demos/tests/perf run the
  cross-built gdk-pixbuf-csource to inline PNGs at build time, which can't
  execute on the build host.
- cairo must have the PDF backend (configure hard-checks cairo-pdf.h).

Runtime note: loading a GTK2 app pulls ~40 shared objects, which needs the
ld.so LD_MAX_OBJS bump (committed separately).

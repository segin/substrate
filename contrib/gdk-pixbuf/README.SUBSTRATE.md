# gdk-pixbuf (2.36.12 — last autotools)

Image loading for GTK+ 2.x.  Cross-built via substrate_sysroot against glib2 +
libpng.  --disable-modules with --with-included-loaders=png builds the PNG
loader into the library (no separate loader .so, so no target
gdk-pixbuf-query-loaders run at build time).  Top-level SUBDIRS trimmed to the
library (thumbnailer needs a loaders.cache --disable-modules never makes); a
shared-mime-info.pc stub satisfies configure.  jpeg/tiff/jasper off (no ports).
Produces libgdk_pixbuf-2.0.so + gdk-pixbuf-2.0.pc.

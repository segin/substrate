# cairo (1.16.0)

2D graphics library — the rendering backend for pango and GTK+ 2.x.
Cross-built (substrate-autotools) against pixman, freetype, fontconfig,
libpng and the X stack (libX11/libXext/libXrender) for the xlib + xlib-xrender
surfaces; PS/PDF/SVG/GL/script/trace/xcb backends disabled.  Builds SUBDIRS=src
only (test/pdiff.h typedefs bool, rejected by gcc16 C23).  --disable-malloc0
returnsnull-style preseeds; pthread forced to -lpthread (substrate gcc has no
-pthread).  Needs the libc pthread_cleanup_push/pop macros (added alongside).
Produces libcairo.so + cairo.pc.

# libXrender (0.9.11)

X Render extension client library — cairo's xlib backend and GTK+ 2.x's GDK
require it.  Cross-built against dist-xorgproto (renderproto) + dist-libX11
via the substrate-autotools helpers.  `--disable-malloc0returnsnull`
(substrate malloc(0) is non-NULL, and the xorg run-test can't execute when
cross-compiling).

Patch 0001-glyph-bufalloc-semicolon: three `BufAlloc(...)` calls in
src/Glyph.c lack a trailing `;` — harmless with the old expression-style
macro, but substrate's libX11 1.8 Xlibint.h uses the modern do-while
`BufAlloc`, which needs it.

# atk (2.28.1)

Accessibility toolkit — a GTK+ 2.x dependency.  glib2-only; cross-built via
the substrate-autotools helpers, assembling a unified sysroot
(substrate_sysroot glib2 libffi zlib) so pkg-config returns sysroot-prefixed
-I/-L instead of the build host`s conflicting glib.  Produces libatk-1.0.so +
atk.pc.

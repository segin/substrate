# glib2 (GLib 2.56.4 — last autotools GLib)

The foundation of the GTK+ 2.x stack (glib/gobject/gio/gmodule/gthread).
Cross-built (substrate-autotools helpers) against dist-zlib + dist-libffi,
--with-pcre=internal (no system PCRE port), NLS via substrate libc gettext.

gcc16 notes: -std=gnu11 (glib pre-dates the C23 `bool` keyword);
--enable-compile-warnings=no (drops glib`s hardcoded -Werror=format-overflow
which gcc16 trips on null-format args).  PYTHON=python3.

Install uses the contrib/automake-pyshim `imp` shim on PYTHONPATH:
automake 1.16`s py-compile imports the removed-in-3.12 `imp` module
(only for get_tag/cache_from_source, forwarded to importlib).  The codegen
tools (glib-genmarshal, glib-mkenums, glib-compile-resources) are python
scripts, so they run on the build host for downstream ports.

# glib1 (GLib 1.2.10)

The base library of GTK+ 1.2.  Source-only patch-series port; `fetch.sh`
downloads + SHA-verifies the GNOME tarball, borrows the substrate-patched
`config.sub`/`config.guess` from the binutils port, and applies the series;
`build.sh` cross-configures (autoconf 2.13, so cross run-tests are preseeded
via env `glib_cv_*`/`ac_cv_*` and two `configure` run-probe branches are
patched to use substrate defaults) and stages into `dist-glib1`.

Patches:
- `0001-configure-cross.patch` — make the "ANSI library prototypes" and
  "POLL* sysdef" run-tests cross-safe (use substrate `<sys/poll.h>` values,
  which match the glib defaults).
- `0002-glib-h-modern-gcc.patch` — `G_GNUC_PRETTY_FUNCTION` must be a string
  literal for the `g_warning(... "...")` concat (gcc16 won't concat
  `__PRETTY_FUNCTION__`), and the i386 byteswap macros use the obsolete
  `__asm__ __const__` qualifier (dropped; `__const__` is not a valid asm
  qualifier in modern GCC).

Threads are disabled (`--disable-threads`): GTK+ 1.2 doesn't need gthread and
it avoids the pthread-internals sizeof probes.  Installs `glib-config`, which
the gtk1 port's `configure` consumes.

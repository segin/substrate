# hexchat (2.10.2 — last autotools release)

The HexChat GTK+ 2 IRC client.  Cross-built via substrate_sysroot against the
full GTK+ 2.x stack (gtk2/gdk-pixbuf/pango/atk/cairo/glib2 + X) plus libdl.
2.12+ is meson-only (substrate has no meson cross harness), so 2.10.2 is used.

Build notes:
- **In-source build**: HexChat's generators hardcode source-relative paths
  (`src/common/make-te` reads `textevents.in`; `src/fe-gtk` reads
  `../../data/*.gresource.xml`), which an out-of-source build can't resolve.
- **Host `make-te`**: the `make-te` text-event generator is a noinst program
  that the cross build compiles for the target (can't run on the host); it is
  rebuilt with the host gcc and touched newer than its sources so the
  generation step runs.
- The GResource step uses the build host's `glib-compile-resources`.
- `--disable-gtktest`/`--disable-glibtest` (cross run-tests); D-Bus,
  libnotify, libproxy, libcanberra, isocodes, sysinfo, NLS, and the Perl/
  Python plugin loaders are disabled (no ports).

**SSL deferred.**  HexChat 2.10.2's `src/common/ssl.c` / `server.c` reach into
OpenSSL 1.0 internals (`ssl->session->sess_cert->peer_rsa_tmp`,
`X509->cert_info->key->algor`, `X509_STORE_CTX->current_cert`) that are opaque
or removed in substrate's OpenSSL 3.x.  Built `--disable-openssl` for now;
SSL support needs the cert/SSL accessor modernization HexChat upstream did
over later releases.

## Runtime status

Builds clean against the GTK+ 2.x stack; the 2.6 MB executable's DT_NEEDED
(libgtk-x11/gdk-x11/pango/cairo/atk/glib2/gdk-pixbuf + X + libdl/pthread/c)
all resolve in the baked image, and that shared-library chain is independently
proven to load and run on substrate (the GTK2 smoke test).  A headless
`hexchat -v` run did **not** crash (no SIGSEGV/TRAP) but **hung** during
startup before printing the version — i.e. in `load_config()`/early init,
before `gtk_init`.  Pinning that down (config init vs. fontconfig first-run
cache build vs. the in-kernel loader relocating ~35 deep libs) and an
interactive smoke test both need a live X server (the `sdm`/`Xfbdev` headless
recipe), tracked as follow-up.

# gtk1 (GTK+ 1.2.10)

The classic GTK+ widget toolkit.  GDK is bundled and Xlib-only — no Cairo,
Pango, ATK, or gdk-pixbuf — so the only prerequisites are the substrate X
client stack (xorgproto, libXau, xtrans, libxcb, libX11, libXext) and the
`glib1` port.

`build.sh` assembles a sysroot from those dist trees, relocates a copy of
`glib-config` so its emitted `-I`/`-L` point at the sysroot, cross-configures
(autoconf 2.13 — sizeof/socklen run-tests preseeded via env), builds only
`gdk` + `gtk` (po/ message catalogs need a gettext that still accepts
1999-era charset names like `iso-8859-9e`; docs/ needs SGML tooling; neither
is part of the runtime), and stages into `dist-gtk1`, OSABI-patching each
`.so` to ELFOSABI_SUBSTRATE.

Patch: `0001-ltconfig-substrate-shared` — same fix as glib1, canonicalizing
`host_os=substrate` to `linux-gnu` in the bundled 2001-era ltconfig so it
produces ELF shared libraries instead of static-only.

Installs `gtk-config` (consumed by GTK+ 1.2 applications), `libgtk-1.2.so` and
`libgdk-1.2.so`.  DT_NEEDED: libgmodule/libglib (glib1), libX11/libXext, and
libdl/libm/libc — all in the substrate stack.

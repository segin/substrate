# libpng (1.6.43)

PNG codec for cairo and gdk-pixbuf (the GTK+ 2.x stack).  Modern-autotools
patch-free port: `fetch.sh` downloads + SHA-verifies and swaps in the
binutils port's substrate config.sub via `substrate_config_sub_fix`;
`build.sh` cross-configures against `dist-zlib`, applies
`substrate_libtool_fix` (teaches the bundled libtool that host_os=substrate
builds shared ELF), and stages `dist-libpng` with `.so` OSABI patched to
ELFOSABI_SUBSTRATE.  Helpers live in `contrib/substrate-autotools.sh`.

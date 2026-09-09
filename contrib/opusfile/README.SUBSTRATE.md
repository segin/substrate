# opusfile on Substrate

opusfile 0.12 (xiph.org), the high-level Ogg Opus decoder layered on
libogg + libopus.

## Why it is here

`sox` has an Opus reader (`src/opus.c`) that includes `<opusfile.h>`.
Nothing else in the tree provides that header, so before this port sox
configured with `HAVE_OPUS` false and shipped with no Opus support.  That
is invisible in a build log -- an optional codec that fails its probe is
not an error -- which is how it went unnoticed while `libopus` itself was
staged and correctly ordered ahead of sox.

## Build notes

- Configured `--host=i386-unknown-linux-gnu` so the bundled libtool emits
  a shared library; `CC` is still the substrate cross gcc, so the output
  is a substrate binary (OSABI 0x40).
- `--disable-http` drops the openssl dependency.  It only provides URL
  streaming, which sox does not use, and it would otherwise pull
  `libopusurl` into the dependency graph for nothing.
- `-fno-stack-protector`: substrate's `__stack_chk_fail_local` lives only
  in crt0 (hidden, per executable), so `-fstack-protector` in a shared
  library leaves it undefined at link time.  opusfile's configure has no
  `--disable-stack-protector`, unlike libopus's, so the flag goes in
  CFLAGS directly.

## The subdirectory-include trap

opusfile installs its header as `include/opus/opusfile.h` and its
`opusfile.pc` says `Cflags: -I${includedir}/opus`.  With no
`PKG_CONFIG_SYSROOT_DIR` that expands to an absolute `/usr/include/opus`
-- the *build host's* directory.  This bites twice:

- building opusfile itself, whose sources include `<opus_multistream.h>`
  by bare name from libopus;
- building sox, whose `src/opus.c` includes `<opusfile.h>` by bare name.

Both are handled with an explicit `-I${SR}/include/opus` rather than
trusting the `.pc`.  See `docs/contrib-ports.md` for the general shape of
this failure -- it has now cost several multi-hour CI runs under other
names (pixman, disasterparty, SDL2/freetype2).

## Dependencies

libogg >= 1.3, libopus >= 1.0.1.  Both must be staged in the cross
sysroot first; `build.sh` asserts on their `.pc` files.

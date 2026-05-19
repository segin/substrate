# qman (plp13/qman)

[plp13/qman] is a TUI man-page reader built on `mandoc` for parsing
and `ncurses` for the terminal UI.

Upstream: <https://github.com/plp13/qman>
Pinned version: **v1.5.1**
License: BSD 2-Clause (see `build/qman-<ver>/LICENSE` after fetch).
Substrate vendoring: tarball + patch series.

## Status

**STAGED — not yet building.** The source is fetched and extracted
by `./fetch.sh`, but `./build.sh` exits non-zero with the dependency
list.  The blockers are below in dependency order.

## Blockers

### 1. meson(1) + ninja(1)

qman's build system is meson, which is Python-based.  Substrate
doesn't yet ship a Python port (the toolchain comfortably builds
gcc and friends without one, so it hasn't been on the critical
path), so meson + ninja aren't available either.

**Substitution path.**  Hand-author a Makefile that mirrors
`src/meson.build`'s source list and link line — qman's binary is
ultimately a single `qman` ELF that links ncursesw + mandoc and a
handful of helper .c files.  Drop the Makefile into `patches/` as
`0001-substrate-makefile.patch` and drive `make` from `build.sh`
instead of `meson + ninja`.

### 2. cogapp (cog.py)

`src/config.c.cog` and `src/config.h.cog` are processed by Python
`cog` (cogapp) at build time to expand option-table boilerplate.
Without cog we have nothing to define the option table at compile
time.

**Substitution path.**  Run cog on the BUILD host during `fetch.sh`
(the build host has Python), snapshot the generated `config.c` and
`config.h` into `patches/`, and ship them as part of the patch
series.  The substrate-target build then skips the codegen step
entirely — the generated files are already on disk.

### 3. libbsd-overlay

qman's meson.build pulls `dependency('libbsd-overlay')` so the
Linux-built version gets `strlcpy`, `strlcat`, `arc4random`, etc.
on glibc systems that lack them.  Substrate's `libc` already
implements these natively, so `libbsd` adds nothing for us.

**Substitution path.**  Patch `src/meson.build` (or the
hand-authored Makefile from blocker #1) to drop the libbsd
dependency.  Substrate libc covers the surface.

### 4. zlib

qman opens `.gz`-compressed man pages directly via zlib.  Substrate
has no zlib port today.

**Substitution path A.**  Add `contrib/zlib/` as a new contrib port
— zlib is small (~30KB source) and uses a hand-rolled configure.
Once that exists, qman picks it up.

**Substitution path B.**  Patch `qman.c` to drop the gzopen/gzclose
path and only read uncompressed pages.  Substrate's `usr.share.man`
ships pages uncompressed anyway, so this is acceptable for an
initial port.

### 5. liblzma

xz-compressed pages.  Optional in qman's build; substrate skips it
by leaving liblzma undetected.  No action needed.

### 6. ncursesw vs ncurses

qman wants the wide-character variant of ncurses (`libncursesw`).
substrate's `contrib/ncurses` builds the upstream tree but the
shipped library name needs checking.

**Substitution path.**  After contrib/ncurses builds, verify with
`find ${STAGE1_PREFIX}/i386-unknown-substrate/lib -name 'libncurses*'`.
If only `libncurses.{a,so.6}` ships, patch `contrib/ncurses/build.sh`
to pass `--enable-widec` to ncurses' configure.

## Once these land

Flip the `exit 1` at the end of `build.sh`, populate the actual
configure + build + install commands, and add `qman` to
`build.sh`'s `DEFAULT_CONTRIB` list.

[plp13/qman]: https://github.com/plp13/qman

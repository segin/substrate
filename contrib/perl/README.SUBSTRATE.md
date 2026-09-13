# perl on Substrate

Perl 5.44.0, cross-built with perl-cross 1.6.5.

## Why perl-cross

perl's own `Configure` learns the target by running test programs, which a
cross build cannot do.  perl-cross replaces it with a configure that only
compiles and links, plus per-version fixes to the perl tree; `fetch.sh`
unpacks it over the perl source.  It builds a host `miniperl` first and uses
it to drive the target build.

## Patches and hints

- `substrate.hints`, installed by `fetch.sh` as `cnf/hints/substrate`.
- `0001-errno-find-target-errno-h-when-cross-compiling.patch`.
  `ext/Errno/Errno_pm.PL` runs under the host miniperl, so `$^O` is `linux`,
  and its linux branch reads `/usr/include/errno.h` -- the build host's glibc
  header.  The target compiler cannot preprocess it (`features.h: No such
  file`), and `Errno.pm` came out with no constants.  With the patch, a
  cross build without a sysroot asks the target compiler (`-M`) which
  `errno.h` it includes; the generated `Errno.pm` has substrate's 88
  constants with substrate's values (`ENOENT` 2, `EAGAIN` 11, `ENOSYS` 38).

## Build notes

- `--targetarch=i386-substrate`: perl-cross otherwise asks `config.sub`,
  which does not know substrate, and stops with "cannot determine target
  platform".
- `-Dosname=substrate` selects the hints file.  **Do not use `--hints=`**:
  in perl-cross 1.6.5, `configure_hint.sh` calls `tryhints 'hint' "$h"` and
  `tryhints` only reads its first argument, so it looks for a hints file named
  `hint`, finds none, and silently applies no hints at all.
- The hints supply what perl-cross cannot probe.  Without `d_nanosleep`,
  `config.h` gets a bare `# HAS_NANOSLEEP` line and the build stops.  They
  also set `st_ino_size=8`, because the probe cannot run target code and
  falls back to 4, while substrate's `ino_t` is 64-bit.
- perl builds in its source tree, so a rebuild starts from a fresh
  `fetch.sh` extraction.  Re-running `make` in a tree whose first build
  failed can loop on "Stale pm_to_blib, please re-run make": the stale stamp
  satisfies an order-only prerequisite, and the recovery `rm $<` has no
  operand to remove.

## Substrate-side fixes

- `<stdint.h>`'s `UINT64_C` and friends expanded to casts, which `#if` cannot
  evaluate; perl's `handy.h` uses `UINTMAX_C` in `#if` and failed building
  DynaLoader with "missing binary operator before token 1ULL".
- libc had no `ctermid()` or `L_ctermid`, which the POSIX extension wraps.

Both were fixed in libc rather than patched around here.

## Installed files

perl installs its XS extensions read-only (0555).  `build.sh` lifts the
owner write bit to stamp `ELFOSABI_SUBSTRATE` into them and restores the
mode; `dd` cannot write through a read-only file, and under `set -e` the
first one ended the build with only `bin/perl` stamped.

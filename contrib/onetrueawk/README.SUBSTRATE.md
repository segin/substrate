# One True Awk — Substrate vendor copy

This is an unmodified vendor copy of Brian Kernighan's awk
("One True Awk").  It's the BWK awk shipped on macOS, the BSDs, and
many other Unix systems.

## Upstream

- Repository: https://github.com/onetrueawk/awk
- Vendored commit: `5739fd79bcfc75ba7526773d0cf634521f8aca3c` (2026-04-26)
- Licence: see `LICENSE` — BSD-style ("Lucent" notice), GPL-incompatible
  with no advertising clause.  Compatible with substrate's mixed
  permissive tree.

## What lives here

Upstream sources at their upstream paths:

```
awk.h           awk.1        awkgram.y     proto.h
main.c          lex.c        b.c           parse.c
lib.c           run.c        tran.c        maketab.c
LICENSE         README.md    FIXES         ChangeLog
```

Plus three files that are *generated* upstream by yacc + the
`maketab` host program and that we check in pre-built so the
substrate build doesn't need bison or a working host C compiler
just to lay down the parser tables:

```
awkgram.tab.c   awkgram.tab.h   proctab.c
```

`testdir/` (the upstream regression corpus, ~7.5 MB) is **not**
vendored.  If you need it, pull it from the upstream repo into a
scratch directory — `bin/awk` doesn't reference it.

## Regenerating the parser

If you ever bump upstream and need to re-generate the parser tables:

```sh
cd contrib/onetrueawk
bison -d -b awkgram awkgram.y                    # → awkgram.tab.{c,h}
cc -O2 -Wall maketab.c -o /tmp/maketab           # host tool
/tmp/maketab awkgram.tab.h > proctab.c           # → proctab.c
rm /tmp/maketab
```

Both generated files are deterministic given the bison version — the
substrate tree pins bison 3.8.x output today.

## How `bin/awk` consumes this

`bin/awk/Makefile` lists the C files by basename and uses `VPATH` to
pull them from this directory.  Object files land in `bin/awk/`, so
the contrib tree stays clean.  See `bin/awk/Makefile`.

## Substrate-side prerequisites added for this vendoring

Wiring up awk surfaced several gaps in substrate's C library and
headers.  These were closed in libc/headers (not via patches to
upstream awk) so other vendored tools can reuse them:

- `<ctype.h>`: added `isblank(3)` (C99).
- `<stdlib.h>`: added `mblen`/`mbtowc`/`wctomb` (C89 stateless
  multibyte conversion); added `random`/`srandom` (POSIX, routed
  through `rand`/`srand` since substrate's `rand` already covers
  the BSD `[0, 2^31-1]` range).
- `<signal.h>`: exposed `siginfo_t`, the `SI_*` / `FPE_*` / `ILL_*` /
  `SEGV_*` / `BUS_*` / `TRAP_*` constants, and made `struct sigaction`'s
  handler a union of `sa_handler` and `sa_sigaction` so SA_SIGINFO
  handlers can be installed from userland.  (The kernel side already
  delivered the extended frame.)

## Local patches

None.  We compile upstream sources verbatim.  If a fix is needed,
prefer:

1. Patching upstream and bumping the vendored commit, OR
2. Adding a `.patch` file here and applying it in `bin/awk/Makefile`
   before compile — keep diffs small and labelled so the next
   re-vendoring isn't a guessing game.

Do **not** edit the upstream sources in place — re-vendoring would
silently lose the changes.

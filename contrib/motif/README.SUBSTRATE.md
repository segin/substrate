# OpenMotif 2.3.8 — substrate port

Cross-builds the **Motif libraries** — `libXm` (the widget set) and `libMrm`
(the resource manager that instantiates UIL-compiled interfaces at run time) —
plus their headers, for the substrate i386 target.

## Scope: libraries only

This port ships `libXm.a`, `libMrm.a`, the `Xm/` and `Mrm/` headers, and the
`xmbind.alias` virtual-key binding data.  It does **not** build the Motif
clients (`mwm`, `uil`, `xmbind`).

The reason is the `tools/wml` build-time toolchain.  Motif generates parts of
its UIL tables with `wml` / `wmluiltok` / `wmldbcreate`, which are *build-host*
programs (like `config/util/makestrs`).  Under a cross build the recursive make
compiles them with the cross compiler, so they (a) can't run on the build host
and (b) `wmluiltok` — a flex lexer — links with no `main`.  No library depends
on their output: the UIL widget tables are pre-generated and checked into the
tarball.  We therefore build `lib/Xm` and `lib/Mrm` directly and skip `tools/`
and `clients/`.  This is enough to compile and link Motif application programs
(the substrate use case is `motifgpt`).

Both static and shared libraries are produced (`libXm.so.4`, `libMrm.so.4`).
libtool's `case $host_os` has no substrate branch, so by default it sets
`build_libtool_libs=no` and `--enable-shared` yields only `.a`; the shared
build is enabled by applying `../substrate-libtool-shared.sh` to the generated
`configure` (the sed equivalent of the `0002-libtool-configure-substrate-shared`
patch the X libraries carry).  `regcomp`/`regexec` are absorbed into `libXm.so`
from `libregex.a`, so it is self-contained for regex.

## Build-time fixups (no patch series)

All adaptations live in `fetch.sh` / `build.sh`; there is no `patches/` series:

- **`config.sub`**: taught the `*-substrate*` OS triplet inline in `fetch.sh`.
- **`--disable-xft`**: no Xft/fontconfig port yet; core X bitmap fonts only.
  (Also stops `ac_find_xft.m4` from latching onto the build host's `libXft`.)
- **`substrate-cross.cache`**: seeds the `AC_TRY_RUN` probes configure can't run
  under a cross build (e.g. `setpgrp` arity).
- **`-D_POSIX_THREAD_SAFE_FUNCTIONS=200809L`**: makes `<X11/Xos_r.h>` select the
  5-argument `getpwnam_r` prototype that matches substrate's libc.
- **`-std=gnu89 -Wno-error`**: the Motif tree is K&R/C89 and trips modern GCC.
- **host-rebuild of `config/util/makestrs`**: the string-table generator runs on
  the build host, so after pass 1 it is recompiled with the host `gcc` and
  touched newest so the resumed make doesn't relink it for the target.

## Layout

    fetch.sh                download + sha256 verify + extract + config.sub fixup
    build.sh                configure + two-pass make + libraries-only install
    substrate-cross.cache   cross AC_TRY_RUN seed values
    series                  (empty — no source patches needed)

Staged output: `dist-motif/usr/{lib/lib{Xm,Mrm}.a,include/{Xm,Mrm},lib/X11/bindings}`.

## Dependencies

The staged X client stack: `libX11`, `libXext`, `libXt`, `libXmu`, `libXpm`
(and transitively `libICE`, `libSM`, `libxcb`, `libXau`).

# matwm2 — substrate port

A simplistic overlapping X11 window manager.  Upstream:
https://github.com/segin/matwm2.  No tagged releases; pinned to
the master snapshot at fetch time.

Built with all optional features disabled (no Xext/shape,
no Xinerama, no Xft) — substrate doesn't yet have libXinerama or
libXft + freetype.  The bare X11 window-manager surface is enough
for matwm2 to run as the WM on top of substrate's Xfbdev.

## Build

    cd contrib/matwm2
    ./fetch.sh
    ./build.sh

Stages dist-matwm2/usr/bin/matwm2 and /etc/matwmrc (the default
config; matwm2 reads ~/.matwmrc first and falls back to
/etc/matwmrc).

## Substrate-specific

* Upstream's hand-rolled `configure` doesn't know about
  pkg-config-less cross sysroots; `--force-x11` skips the library
  probe, explicit `-I`/`-L` flags go through `--cflags`/`--ldflags`.
* `-std=gnu99` — matwm.h does `typedef char bool;` + `enum { false,
  true };`, which conflicts with C2x's built-in `bool` keyword.
* `-lsys` is added to the link line: substrate's `setsid(3)` lives
  in libsys, not libc.
* `Makefile.in`'s `-DVERSION="\"0.1.2pre3\""` shell-quotes get
  munged at fetch time so cc actually sees a string literal.

# ncurses — Substrate port

ncurses 6.4 cross-built for substrate via the stage-1 GNU
toolchain.  Replaces lib/curses (the link-time stub that's been
on the image up to now) with a real terminfo backend.

## Build chain

- `fetch.sh` — fetch ncurses-6.4.tar.gz from invisible-island,
  SHA-verify, extract, apply the substrate patch series.
- `build.sh` — `./configure --host=i386-unknown-substrate ...`,
  `make`, stage into `${SUBSTRATE_TOP}/dist-overlay/dist-ncurses/usr/`.

## Configure flags (rationale)

  --host=i386-unknown-substrate     substrate cross
  --prefix=/usr
  --with-cxx-binding                build libncurses++(w), the C++
  --with-cxx-shared                 binding, as shared libraries
                                    against the shared libstdc++
  --without-ada                     skip Ada binding
  --without-tests                   skip test programs
  --with-shared                     produce libncurses.so.6
  --with-normal                     also produce static .a
  --without-debug                   no -DTRACE
  --disable-stripping               leave stripping to image
                                    bake step
  --with-termlib                    split off libtinfo (for
                                    consumers that only want
                                    terminfo, not full curses)
  --without-manpages                save space
  --enable-overwrite                drop curses.h into
                                    /usr/include directly,
                                    matching the rest of the
                                    substrate userland layout

## Substrate adaptations

See `series` / `patches/`.

  0001-config-sub-substrate.patch   standard substrate OS-name
                                    registration

Anything else (header conflicts, missing libc bits, etc.)
becomes a numbered patch as it surfaces.

## Image layout

  /usr/bin/{tic,tput,clear,reset,...}        (overrides the
                                              hand-rolled
                                              bin/clear,
                                              bin/reset stubs)
  /usr/lib/libncurses.so.6
  /usr/lib/libtinfo.so.6  -> libncurses.so.6 (or split file
                                              depending on
                                              --with-termlib)
  /usr/include/{curses,term,ncurses,termcap}.h
  /usr/share/terminfo/...                    (data files —
                                              already populated
                                              from etc/terminfo)

## Retirement of the lib/curses stub

Once ncurses is on the image, the link-time symbol probes that
zsh/less/vi do at configure time resolve against the real
ncurses ABI.  At runtime setupterm() actually reads the
terminfo binaries we shipped — terminal handling stops being a
no-op.

lib/curses is kept in-tree for the embedded / no-ncurses
profile, but isn't installed when contrib/ncurses is built.

That now holds for its headers too.  `curses.h`, `term.h` and
`termcap.h` used to sit in the top-level `include/`, which is mirrored
into the cross sysroot unconditionally, so the 1.5 KB stubs silently
replaced ncurses's real headers there whenever the native headers were
synced after this port -- `scripts/sync-sysroot.sh` run with no
arguments does exactly that.  They live in `lib/curses/` now and are
installed only by that library's own `install` target.

## Wide-character build

Configured `--enable-widec`, so the libraries are `libncursesw`,
`libformw`, `libmenuw` and `libpanelw`, and `curses.h` is built with
`NCURSES_WIDECHAR 1`, declaring the `cchar_t` API (`mvin_wchnstr`,
`getcchar`, `setcchar`, `mvadd_wchnstr`, ...).  mc cannot be built without
it -- its ncurses backend draws shadows with those calls unconditionally --
and it is what gives nano and less multibyte (UTF-8) text.

- `--with-termlib=tinfo` keeps the terminal-info library as plain `libtinfo`
  rather than `libtinfow`, so ports linking `-ltinfo` alone (gdb) are
  unaffected.
- The headers keep their usual names and still install flat into
  `/usr/include`.
- The narrow libraries are built too, in a second configure pass without
  `--enable-widec`, and staged as real shared objects alongside the wide ones:
  `libncurses.so.6`, `libform.so.6`, `libmenu.so.6`, `libpanel.so.6` and
  `libncurses++.so.6`, with `libcurses.so` pointing at `libncurses.so`.  So
  `-lncurses` (and `-lcurses`, `-lform`, ...) links the narrow library and
  `-lncursesw` the wide one, as on Debian.  A port that wants multibyte text
  links the `w` library explicitly (mc and nano do).
- One header set, the wide build's, serves both.  It differs from the narrow
  one only in what `NCURSES_WIDECHAR` switches on, and `WINDOW`'s wide-only
  members sit at the end of `struct _win_st` under that switch, so a narrow
  consumer (`NCURSES_WIDECHAR` 0 unless it asks for `_XOPEN_SOURCE_EXTENDED`)
  sees the narrow library's layout.  The narrow build's `libtinfo` is not
  staged: the wide build's exports every symbol it does, plus the
  extended-colour `*2` variants.
- The wide build needs POSIX `tsearch`/`tfind`/`tdelete` from libc: extended
  colour pairs, which `--enable-widec` turns on, live in a binary tree in
  `ncurses/base/new_pair.c`.

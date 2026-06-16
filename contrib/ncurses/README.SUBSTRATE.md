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
  --without-cxx-binding             skip libncurses++
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

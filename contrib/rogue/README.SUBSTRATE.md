# rogue

Rogue: Exploring the Dungeons of Doom -- the original, by Michael Toy, Ken
Arnold and Glenn Wichman, in the 5.4.4 release maintained by the Roguelike
Restoration Project.

Upstream: <http://rogue.rogueforge.net/> (no longer answering)
Pinned version: **rogue 5.4.4**
License: BSD-style (see `build/rogue5.4.4/LICENSE.TXT`).
Substrate vendoring: tarball only -- there is no patch series.

The tarball is fetched from Fedora's lookaside cache, which names a file
by its MD5; MacPorts' distfiles mirror carries the same bytes.

## Build

```
./fetch.sh
./build.sh
```

Needs `contrib/ncurses` staged.  Produces, under
`dist-overlay/dist-rogue/`:

- `/usr/bin/rogue`
- `/var/games/rogue/rogue.scr` -- the scoreboard
- `/usr/share/man/man6/rogue.6`
- `/usr/share/doc/rogue/` -- *A Guide to the Dungeons of Doom*

## Substrate-specific notes

- **The scoreboard is shared**, so `build-rootfs.sh` makes the game
  setgid `games` and `/var/games/rogue` group-writable, inside the image.
  `--enable-setgid` is deliberately not passed to configure: it makes
  `make install` chgrp the staged binary, which an unprivileged build
  cannot do.
- **The manual page's directory** is chosen by the Makefile from whether
  `/usr/share/man/man6` exists on the build machine; `build.sh` moves the
  page into `man6` if it landed elsewhere.
- The 1980s K&R C builds as it is, because the toolchain defaults C to
  gnu17.

## Playing

A terminal of at least 24 by 80 and a `TERM` that terminfo knows.
`rogue -s` prints the scoreboard; `rogue savefile` restores a game saved
with `S`.

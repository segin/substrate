# angband

Angband (<https://rephial.org/>), the dungeon crawl descended from Moria,
with the curses and X11 front ends.

Upstream: <https://github.com/angband/angband/releases>
Pinned version: **Angband 4.2.5**
License: GPLv2 or the Angband licence (see `build/Angband-4.2.5/docs/copying.rst`).
Substrate vendoring: tarball only -- there is no patch series.

## Build

```
./fetch.sh
./build.sh
```

Needs `contrib/ncurses` (the wide-character library) and `contrib/libX11`
with its `libSM`/`libICE` staged.  Produces, under
`dist-overlay/dist-angband/`:

- `/usr/bin/angband`
- `/usr/share/angband/` -- help, screens, fonts, tiles, sounds, icons
- `/etc/angband/` -- `gamedata/` and `customize/`, the text files the
  whole game is defined in

## Substrate-specific notes

- **`--with-private-dirs`.**  Saved games, scores and preferences live
  in `~/.angband/Angband/`.  The game needs no setgid bit and no shared
  writable directory, at the cost of a scoreboard per user.
- **A stand-in `ncursesw6-config`.**  configure asks that script how to
  compile and link with ncurses.  The one the ncurses port installs
  answers for the target's filesystem (`-I/usr/include/ncursesw`), which
  a cross compiler would read as the build machine's; `build.sh` writes
  one that answers for the sysroot, and passes `--disable-ncursestest`
  because the test runs a curses program.
- **`--x-includes`/`--x-libraries`** point `AC_PATH_XTRA` at the sysroot.
- SDL front ends and sound are not built.  The `.mp3` files under
  `sounds/` are installed by upstream's `make install` regardless.

## Playing

- Angband refuses to start outside a UTF-8 locale ("Angband requires
  UTF-8 support").  `/etc/profile` sets `LANG=en_US.UTF-8`, so a login
  shell is fine; a bare `su -c` or a script is not, unless it sets `LANG`.
- `angband -mgcu` for curses, on a terminal of at least 24 by 80;
  `angband -mx11` under X.  With neither, it tries X11 first, then
  curses.
- `-n` starts a new character; Ctrl-X saves and quits.

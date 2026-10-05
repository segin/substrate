# nethack

NetHack (<https://www.nethack.org/>), with the tty and curses interfaces.

Upstream: <https://www.nethack.org/download/3.6.7/>
Pinned version: **NetHack 3.6.7**
License: NetHack General Public License (see `build/NetHack-3.6.7/dat/license`).
Substrate vendoring: tarball only -- there is no patch series.

## Build

```
./fetch.sh
./build.sh
```

Needs `contrib/ncurses` staged, and on the build machine a C compiler
that can produce and run 32-bit programs (`cc -m32`: gcc-multilib),
`bison` and `flex`.  Produces, under `dist-overlay/dist-nethack/`:

- `/usr/bin/nethack` -- the wrapper script
- `/usr/lib/nethack/` -- the game, `recover`, the `nhdat` data archive,
  `symbols`, `license`, `sysconf`
- `/var/games/nethack/` -- `record`, `logfile`, `xlogfile`, `perm`,
  `save/`, and at run time bones and lock files
- `/usr/share/man/man6/{nethack,recover}.6`
- `/usr/share/doc/nethack/Guidebook.txt`

`build-rootfs.sh` makes the game setgid `games` and `/var/games/nethack`
group-writable, inside the image.

## How it is cross-built

NetHack 3.6 cannot cross-compile by itself.  Its build compiles
`makedefs`, `lev_comp`, `dgn_comp` and `dlb` with the one `$(CC)` and
then runs them -- to generate headers, and to compile the dungeon and its
levels into binary files the game reads back as raw C structures.

`build.sh` therefore builds from two copies of the tree:

- `build/host/` -- the four tools, with the build machine's compiler,
  and everything they generate (`onames.h`, `pm.h`, `vis_tab.[ch]`,
  `date.h`, the text databases, every `.lev`, `dungeon`, `nhdat`);
- `build/target/` -- the game and `recover`, with the cross compiler,
  given the generated files and told (`make -o`) not to remake them.

Two things keep the halves consistent:

- **The tools are built for the target's data model** (`-m32`).  A level
  file is the tool's structures written out as they lie in memory, so the
  tool's `long` and pointer sizes have to be the game's.
- **Both get the same feature defines.**  `makedefs` records them in the
  `options` file, which goes into `nhdat`.

The install step is done by hand, because the top-level `install` target
wants to build and run the tools again and writes to the real `HACKDIR`.

## Substrate-specific choices

- **`-DLINUX`**: NetHack's name for a POSIX system with termios and the
  usual BSD extras.
- **`-include sys/ioctl.h`**: `sys/share/ioctl.c` calls
  `ioctl(TIOCGWINSZ)` whenever `<termios.h>` defines the request, but
  includes the header declaring `ioctl()` only for compilers that
  predefine `__linux__` or `BSD`.
- **`REGEXOBJ=pmatchregex.o`**: NetHack's own glob-style matcher for
  `MENUCOLOR`, `MSGTYPE` and autopickup exceptions, not POSIX regular
  expressions.  NetHack's POSIX back end defines functions named
  `regex_compile`, `regex_match` and `regex_free`; substrate's
  `<regex.h>` declares those names, with other signatures, as libregex's
  native interface, and libregex exports them.  Until those names are out
  of the POSIX header's way (and out of the way of a program that
  defines its own), patterns in `.nethackrc` are globs: `*` and `?`.
- **The host build is pinned to `-std=gnu17`**, the cross compiler's
  default; a current host gcc defaults to C23, which rejects NetHack's
  K&R declarations.

## Playing

`nethack` on a terminal of at least 24 by 80.  `OPTIONS=windowtype:curses`
in `~/.nethackrc` (or in `NETHACKOPTIONS`) selects the curses interface;
the default is tty.  `nethack -s` lists scores.

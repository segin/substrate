# xman 1.2.0 — substrate port

`xman` is the Athena-widget manual-page browser.  Produces `/usr/bin/xman`.

## Build
```sh
./fetch.sh
./build.sh
```

Depends on the full Athena stack: xorgproto, libxcb, libXau, xtrans, libX11,
libXext, libICE, libSM, libXt, libXmu, libXpm, libXaw.

Substrate notes (handled in `build.sh`, no source patches):
- Cross-compiling defeats xman's `AC_CHECK_FILE` probe for a man-config file;
  the build seeds `ac_cv_file__etc_man_conf=yes` (substrate's man toolchain is
  mandoc reading `/etc/man.conf`).
- xman's man-config-format switch has no `substrate` case; since mandoc uses
  the BSD-style `man.conf`, `build.sh` folds `substrate` into the `*bsd*`
  branch of the generated `configure`.
- `main.c` uses `bcopy()` without including `<strings.h>`; substrate provides
  `bcopy`, so the build forces the declaration with `-include strings.h`.
- `config.sub` is taught the `substrate` triplet by `fetch.sh`.

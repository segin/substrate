# xterm on substrate

Upstream:  https://invisible-island.net/xterm/
Pinned:    410
Tarball:   `https://invisible-island.net/archives/xterm/xterm-410.tgz`
SHA-256:   `7ba9fbb303dd3d95d06ca24360d019048d84e5822dc6fe722cd77369bdbf231f`

## Why

`xterm` is the classic X11 terminal emulator.

## Scope

- `/usr/bin/xterm`, `/usr/bin/resize`
- app-defaults → `/usr/share/X11/app-defaults/{XTerm,XTerm-color}`
- icons / pixmaps

Built with the **core X bitmap fonts** and the **Athena-widget
toolbar**.  TrueType / Xft / fontconfig is disabled
(`--disable-freetype`) — that font stack is not ported.

## Depends on

The full X library chain — `contrib/{xorgproto,libxcb,libXau,
xtrans,libX11,libXext,libICE,libSM,libXt,libXmu,libXpm,libXaw}` —
plus `contrib/ncurses`.  `build.sh` merges every staged X dist tree
into one mini-sysroot (`build/x11root`) and points xterm's
`AC_PATH_X` at it via `--x-includes` / `--x-libraries`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-xterm-io-substrate-termios.patch` — add a substrate entry
  to `xterm_io.h`'s per-OS cascade selecting `USE_POSIX_TERMIOS`
  (substrate provides POSIX `<termios.h>`).
- `0003-main-substrate-pgrp.patch` — add a substrate entry to
  `main.c`'s per-OS cascade selecting `USE_SYSV_PGRP` /
  `USE_SYSV_SIGNALS` (substrate has POSIX `setsid`/`setpgid` and
  the no-argument `setpgrp`).

## Build notes

- `ac_cv_func_setpgrp_void=yes` — substrate's `setpgrp(2)` is the
  POSIX no-argument form; the probe cannot run when cross-compiling.
- `-DHAVE_GRANTPT_PTY_ISATTY` — substrate has Unix98 ptys
  (`/dev/ptmx`, `/dev/pts/N`, `posix_openpt`/`grantpt`/`unlockpt`/
  `ptsname` in libc).  xterm's `get_pty()` only takes that path when
  this macro is set, but `configure` can set it only by *running* a
  probe — impossible cross-compiling, so it fell through to BSD-style
  `pty_search()` over `/dev/pty??` nodes that do not exist
  (`get_pty: not enough ptys`).  Forcing the macro routes `get_pty()`
  through `posix_openpt()` + `ptsname()`.
- Porting xterm completed substrate's `<termios.h>` with the POSIX
  `c_oflag` output-delay constants (`TABDLY`/`TAB0..3`, `CRDLY`,
  `NLDLY`, …) and added the XSI `P_tmpdir` to `<stdio.h>`.

## Notes

xterm is an X *client*; it needs a running X server to be usable.

## Layout

    contrib/xterm/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

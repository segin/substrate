# twm 1.0.12 on Substrate

twm (Tab Window Manager) — the minimal reference X11 window manager.

## Build
```
./fetch.sh        # download + verify + extract + config.sub token
./build.sh        # -> dist-twm/usr/bin/twm
```
Depends on the staged X client library chain (xorgproto, libX11, libXext,
libXt, libXmu, libICE, libSM); build those first.

## Substrate notes
- Standard autotools cross-build, modelled on the xterm port: the staged X
  dist trees are merged into one mini-sysroot (`build/x11root`) and exposed
  through `pkg-config` (`PKG_CONFIG_LIBDIR`).
- twm parses `~/.twmrc` with a yacc/lex grammar (`gram.y`, `lex.l`); `YACC`
  and `LEX` are the build host's `yacc`/`flex`, which emit C compiled by the
  cross compiler.
- Linked as a **PIE** (`-fPIE -pie -Wl,--copy-dt-needed-entries`) so the
  libXt/libXmu `WidgetClass` globals resolve via `R_386_GLOB_DAT` instead of
  `R_386_COPY` — the same reason the xterm port is a PIE.
- `xrandr` is optional in twm and not ported, so configure builds without it
  (`HAVE_XRANDR` undefined); the `xorg-macros` pkg-config warnings are
  harmless (the macros are bundled in the released tarball).

## Result
`/usr/bin/twm`, a PIE needing libX11/libXext/libXt/libXmu/libSM/libICE.
Run it against an X server (e.g. Xfbdev) as the window manager.

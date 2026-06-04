# xkill 1.0.7 — substrate port

`xkill` forces an X server to close a client's connection; you select the
victim window with the mouse (or `-id <window>`).  Produces `/usr/bin/xkill`.

## Build
```sh
./fetch.sh        # download + sha256-verify + extract + config.sub fixup
./build.sh        # cross-configure + make + stage into dist-xkill/usr/
```

Depends on the staged X client stack: xorgproto, libxcb (+ libXau, xtrans),
libX11, libXext, libXmu.  No source patches are needed — stock xkill
cross-compiles clean; `fetch.sh` only teaches the bundled `config.sub` about
the `substrate` OS triplet (the one-token fixup used across contrib).

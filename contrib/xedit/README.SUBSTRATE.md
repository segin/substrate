# xedit 1.2.4 — substrate port

`xedit` is the Athena-widget text editor (it embeds a small Lisp interpreter
for its configuration).  Produces `/usr/bin/xedit`.

## Build
```sh
./fetch.sh
./build.sh
```

Depends on the full Athena stack: xorgproto, libxcb, libXau, xtrans, libX11,
libXext, libICE, libSM, libXt, libXmu, libXpm, libXaw.

Substrate notes (handled in `build.sh`, no source patches):
- The bundled `lisp/mathimp.c` calls the legacy `finite()`.  substrate's
  `<math.h>` provides `isfinite` (as a macro) but not the obsolete `finite`,
  so the build maps `-Dfinite=isfinite`.
- `config.sub` is taught the `substrate` triplet by `fetch.sh`.

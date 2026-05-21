# elinks

ELinks — a feature-rich text-mode web browser (the maintained
"felinks" fork of the original ELinks), built with the QuickJS
JavaScript engine.

Upstream: <https://github.com/rkd77/elinks>
Pinned version: **elinks-0.19.1**
License: GPL-2.0-only (see `build/elinks-<ver>/COPYING`).
Substrate vendoring: tarball + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-elinks/usr/bin/elinks`.

## Build system

elinks 0.19 builds with **Meson**.  Meson is a host-side tool: it
runs on the build host with the host's Python, reads the substrate
cross file that `build.sh` generates, and emits a Ninja build that
cross-compiles elinks.  Nothing from Meson/Ninja/Python is installed
on substrate.

## Dependencies

elinks with the ECMAScript backend (`-Dquickjs=true`) hard-requires
the NetSurf DOM/CSS stack and a few other libraries; all must be
staged in the cross-toolchain sysroot first:

- `contrib/quickjs` — the JavaScript engine (elinks 0.19.1 targets
  exactly Bellard's `quickjs-2025-09-13`).
- `contrib/libcss`, `contrib/libdom` (+ `libhubbub`,
  `libparserutils`, `libwapcaplet`) — DOM + CSS.
- `contrib/sqlite3` — JavaScript `localStorage` backing store.
- OpenSSL + zlib (already in the sysroot).

## Substrate-specific overrides

- **`0001-substrate-portability.patch`**:
  - `meson.build` — link `libpthread` / `libm` / `libdl` explicitly.
    Modern glibc folds these into libc, so upstream never lists
    them; substrate keeps them separate and the static quickjs /
    sqlite3 archives need their symbols.
  - `src/osdep/osdep.c` — define the XSI `P_tmpdir` macro
    (`"/tmp"`), which substrate's `<stdio.h>` does not provide.
- Console-only build: graphics, X, gpm, libevent, IDN, TRE, NLS,
  bittorrent, IPv6 and the doc toolchain are disabled — substrate
  lacks those libraries.

## Status

elinks runs on substrate: it reports its version and renders HTML +
CSS + tables to stdout via `elinks -dump`.  The QuickJS ECMAScript
backend is compiled and linked in (the QuickJS engine itself is
independently verified on substrate by the standalone `qjs` from
`contrib/quickjs`).  Page `<script>` execution happens only in
elinks's interactive mode — `-dump` deliberately disables
ECMAScript — so interactive browsing is the way to exercise
JavaScript.

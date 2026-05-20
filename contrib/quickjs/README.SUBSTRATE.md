# QuickJS

Fabrice Bellard's QuickJS JavaScript engine
(<https://bellard.org/quickjs/>).

Upstream: <https://bellard.org/quickjs/>
Pinned version: **quickjs-2025-09-13** (tarball `quickjs-2025-09-13-2.tar.xz`)
License: MIT (see `build/quickjs-<ver>/LICENSE`).
Substrate vendoring: tarball + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces, under `dist-quickjs/`:

- `/usr/bin/qjs` — the interpreter / REPL
- `/usr/bin/qjsc` — the bytecode / executable compiler
- `/usr/lib/quickjs/libquickjs.a` — static engine library
- `/usr/include/quickjs/{quickjs.h,quickjs-libc.h}`

## Substrate-specific overrides

QuickJS already cross-compiles cleanly via its `CROSS_PREFIX`
knob: the build first compiles a *host* `host-qjsc` (host gcc) to
turn `repl.js` into C, then cross-compiles the engine, `qjs` and
`qjsc` with `i386-unknown-substrate-gcc`.

- **`0001-Makefile-target-cflags-hook.patch`** — adds
  `TARGET_CFLAGS` / `TARGET_LDFLAGS` make variables that are
  appended only to the *cross* compile/link rules.  This lets the
  substrate build carry `-march=i486 -fno-pie` without leaking
  those flags into the host `host-qjsc` build (an x86-64 gcc
  rejects `-march=i486`).

## Notes

This is a standalone JavaScript engine and interpreter.  It is not
wired into the `links` browser — Twibright Links has no scripting
hooks — but `qjs` runs JavaScript on substrate directly and
`libquickjs.a` is available for embedding.

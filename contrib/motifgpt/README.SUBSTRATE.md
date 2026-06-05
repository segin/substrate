# motifgpt — substrate port

[motifgpt](https://github.com/segin/motifgpt) is a Motif/X11 GUI client for LLM
chat APIs, built on the Motif widget set over the `disasterparty` backend.  This
is the top of the "Motif and friends" stack.

## Build

An autotools project with no release tags, so `fetch.sh` pins an exact upstream
commit and runs `autoreconf` on the build host (its `AX_PTHREAD` macro is pulled
from the host's `autoconf-archive`).

`build.sh` cross-configures with `PKG_CONFIG_LIBDIR` pinned at the staged dist
trees of every dependency so no host library leaks in.  Two cross-build
accommodations:

- **`substrate-cross.cache`** seeds the probes a cross build can't run:
  `AC_PATH_XTRA`'s X-detection program, and the `libXm`/`libXt` `AC_CHECK_LIB`
  link tests (which would fail because `libXm.a` needs the whole X chain that
  `AC_CHECK_LIB` doesn't add).  The X stack and Motif are present in the cross
  sysroot, so the answers are asserted directly.
- **CFLAGS** demote the legacy-C diagnostics GCC 16 promotes to hard errors
  (`-Wno-error=implicit-function-declaration` &c) back to warnings — the upstream
  uses a function before its forward declaration in the same TU.

The `motifgpt_LDADD` from configure is `-lXm -lXt -lX11 ... $LIBCURL_LIBS
$LIBCJSON_LIBS $DISASTERPARTY_LIBS -ldl`; `LIBS` supplies the rest of libXm's
transitive deps (`-lXmu -lXext -lXpm -lSM -lICE -liconv -lregex`).  With the
Motif stack built shared, the binary dynamically links `libXm.so.4`,
`libcurl.so.4`, `libcjson.so.1`, and `libdisasterparty.so.5` (resolved by ld.so
at run time).

The three sample plugins (`plugins/*.so`, built by the upstream `all-local`
rule) are staged under `/usr/lib/motifgpt/plugins`.

## Verified running

motifgpt runs natively on substrate against the **Xfbdev** framebuffer X server
(`etc/x.sh`-style launch, `DISPLAY=:0`): it connects, builds its Motif widget
tree, and enters `XtAppMainLoop`, rendering the chat window (conversation pane,
input box, Send button, menu bar).  A guest CPU with RDRAND (`-cpu …,+rdrand`)
is needed so the kernel entropy pool seeds — otherwise `curl_global_init`'s
OpenSSL seeding blocks the process at startup.

## Dependencies

- Motif (`contrib/motif`: `libXm`/`libMrm`)
- X toolkit: `libXt`, `libXmu`, `libXext`, `libXpm`, `libSM`, `libICE`, `libX11`
- `disasterparty` (`contrib/disasterparty`) -> `libcurl`, `libcjson`
- `libiconv`, `libregex`, `libpthread`, `libdl` from the base system
- libc: `wordexp(3)` and `readdir_r(3)` (added for this stack)

## Running

motifgpt is an X client; it displays against substrate's Xfbdev framebuffer X
server (see "Verified running" above) or any X server reachable over the
network.

## Layout

    fetch.sh                pinned-commit download + verify + autoreconf + config.sub fixup
    build.sh                cross-configure + make + install binary/plugins
    substrate-cross.cache   X / libXm / libXt probe seeds
    series                  (empty — no source patches needed)

Staged output: `dist-motifgpt/usr/bin/motifgpt` + `usr/lib/motifgpt/plugins/*.so`.

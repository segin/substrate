# cJSON 1.7.19 — substrate port

[cJSON](https://github.com/DaveGamble/cJSON) is an ultralightweight JSON parser
in ANSI C.  Ported here because both `disasterparty` and `motifgpt` depend on
`libcjson`.

## Build

cJSON has no configure step and is just two translation units (`cJSON.c` and the
optional `cJSON_Utils.c`).  `build.sh` compiles them with the cross compiler
straight into static archives — no autotools/CMake, no host-vs-target tool
juggling:

- `libcjson.a` — the core parser (`cJSON.o`).
- `libcjson_utils.a` — JSON Pointer / Patch / Merge-Patch helpers
  (`cJSON_Utils.o` + `cJSON.o` so it stands alone).

Headers install to `/usr/include/cjson/{cJSON.h,cJSON_Utils.h}` — the `cjson/`
subdirectory consumers `#include <cjson/cJSON.h>` expect.

`pkg-config` files (`libcjson.pc`, `libcjson_utils.pc`) are hand-written, since
upstream only generates them via CMake's `configure_file`.

Both static archives and shared objects are produced.  cJSON has no libtool, so
the shared link is an explicit `-shared -Wl,-soname,libcjson.so.1` step (matching
upstream's CMake SOVERSION 1): `libcjson.so.1 -> libcjson.so.1.7.x` plus the
`libcjson.so` link-time symlink.

## Layout

    fetch.sh   download + sha256 verify + extract
    build.sh   cross-compile -> libcjson.a + libcjson_utils.a + headers + .pc
    series     (empty — no source patches needed)

Staged output: `dist-cjson/usr/{lib/libcjson*.a,lib/pkgconfig,include/cjson}`.

# sdl12-compat on Substrate

sdl12-compat 1.2.76, cross-built for `i386-unknown-substrate`.

Provides the SDL 1.2 ABI (`libSDL-1.2.so.0`) implemented on top of SDL2 —
which on substrate is itself [`../sdl2-compat`](../sdl2-compat), which is in
turn implemented on SDL3.  The full run-time stack is:

    app -> libSDL-1.2.so.0 -> libSDL2-2.0.so.0 -> libSDL3.so.0 -> X11 / audio

This is what lets 1.2-era SDL software build and run on substrate without
anyone maintaining a real SDL 1.2.

## Build

    ./fetch.sh && ./build.sh

Stages into `dist-overlay/dist-sdl12-compat`.  Requires [`../sdl3`](../sdl3)
and [`../sdl2-compat`](../sdl2-compat) staged first: it needs SDL2's *headers*
at build time and **dlopens `libSDL2-2.0.so.0` at run time** (no `DT_NEEDED`),
so both must be present on target.

## Substrate patches

1. **`0001-cmake-allow-static-builds-on-substrate.patch`** — `STATICDEVEL` is
   gated to `CMAKE_SYSTEM_NAME MATCHES "Linux"`.  Same change, and same
   reasoning, as the sdl2-compat port carries.

## Notes

* `find_package(SDL2 CONFIG)` reports "Could NOT find SDL2 (missing:
  SDL2_DIR)" during configure.  That is harmless: `build.sh` passes
  `SDL2_INCLUDE_DIR`/`SDL2_INCLUDE_DIRS` explicitly, which is the documented
  escape hatch, and headers are all this build needs from SDL2.
* Tests are off (`SDL12TESTS=OFF`); the 1.2 test programs pull in extra
  link-time machinery that is not interesting here.

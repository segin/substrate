# sdl2-compat on Substrate

sdl2-compat 2.32.70, cross-built for `i386-unknown-substrate`.

**This port replaces the old `contrib/sdl2`.**  It provides the SDL2 ABI —
same `libSDL2-2.0.so.0` soname, same headers, same `sdl2.pc` — implemented on
top of SDL3 rather than as a standalone SDL2.  Existing SDL2 consumers
(PsyMP3) link it unchanged.

    app -> libSDL2-2.0.so.0 (this port) -> libSDL3.so.0 -> X11 / audio

## Build

    ./fetch.sh && ./build.sh

Stages into `dist-overlay/dist-sdl2-compat`.  Requires [`../sdl3`](../sdl3) to
be built and staged first: sdl2-compat needs SDL3's *headers* at build time and
**dlopens `libSDL3.so.0` at run time** (there is no `DT_NEEDED` on it), so SDL3
must also be present on target.

## Substrate patches

1. **`0001-cmake-allow-static-builds-on-substrate.patch`** — the static
   library is gated to `CMAKE_SYSTEM_NAME MATCHES "Linux"`.  Substrate is an
   ELF platform on the same GNU toolchain whose CMake platform module defers to
   `Platform/Linux` outright, so nothing about a static build differs; it is
   just not in the list.  The SDL2 port this replaces shipped `libSDL2.a`, and
   dropping it would be a regression.

## Notes

* **`sdl2.pc` is published in addition to `sdl2-compat.pc`.**  Upstream names
  its pkg-config file `sdl2-compat.pc` so it can sit beside a real SDL2 without
  colliding.  On substrate there is no other SDL2 — this port *is* SDL2 — and
  every consumer asks pkg-config for `sdl2`.  The file contents are already
  the correct SDL2 answer, so `build.sh` copies it under that name after
  install.  Without it the SDL2 that ports actually link is invisible to
  pkg-config.
* **No substrate-specific source changes were needed.**  sdl2-compat is a pure
  API shim; everything platform-specific lives in SDL3 underneath it.
* The X client trees are merged into `build/x11root` because
  `SDL2COMPAT_X11=ON` does a `find_package(X11 REQUIRED)`.

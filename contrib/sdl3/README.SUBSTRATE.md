# SDL3 on Substrate

SDL 3.4.14, cross-built for `i386-unknown-substrate`.

SDL3 is the base of substrate's SDL stack.  Nothing links it directly yet —
the SDL2 and SDL 1.2 APIs are provided by [`../sdl2-compat`](../sdl2-compat)
and [`../sdl12-compat`](../sdl12-compat), which sit on top of it:

    app -> libSDL-1.2.so.0 -> libSDL2-2.0.so.0 -> libSDL3.so.0 -> X11 / audio

## Build

    ./fetch.sh && ./build.sh

Stages into `dist-overlay/dist-sdl3`.  SDL3 dropped autotools, so this is a
CMake build driven by the reusable toolchain at
`contrib/cmake/substrate.toolchain.cmake` — which names the platform
`Substrate` (not Linux) and ships a `Platform/Substrate` module that defers to
`Platform/Linux` for ELF link/soname/rpath conventions.

The X client libraries are merged into a mini-sysroot at `build/x11root`, so
the X chain (`libX11`, `libXext`, `libXcursor`, `libXi`, `libXfixes`,
`libXrender`, `libXScrnSaver` and their deps) plus `libiconv` must be built
first.

## What is enabled

| Subsystem | On substrate |
|---|---|
| Video | X11 (dlopens `libX11.so.6`) + dummy + offscreen |
| Audio | `netbsd` backend on `/dev/audio` + dummy + disk |
| Threads | pthreads (`libpthread`) |
| loadso | `dlopen` (`libdl`) |

Off: wayland, kmsdrm, vulkan, opengl/opengles, alsa, pulseaudio, pipewire,
jack, sndio, oss, dbus, ibus, libudev, liburing, hidapi, joystick, haptic.
None of them have anything to talk to on target.

`SDL_X11_XRANDR` is off because libXrandr is not ported; SDL3 treats it as
optional (no display-mode switching).

## Substrate patches

1. **`0001-platform-add-SDL_PLATFORM_SUBSTRATE.patch`** — declares
   `SDL_PLATFORM_SUBSTRATE` from the cross compiler's `__substrate__`
   predefine.  SDL already sets `SDL_PLATFORM_UNIX` from `__unix__`, but
   nothing named the platform itself.
2. **`0002-audiodev-default-to-dev-audio-on-substrate.patch`** — substrate's
   audio device is the Sun/NetBSD-style `/dev/audio`, not the OSS `/dev/dsp`.
   Without this `SDL_EnumUnixAudioDevices()` finds zero output devices and
   opening the default device fails, so no audio callback ever runs.
3. **`0003-cmake-recognise-substrate-and-route-audio-to-netbsd.patch`** —
   `SDL_DetectCMakePlatform()` had no Substrate case, so the audio chain
   (which starts `if(NETBSD)`) gave substrate no backend at all, and
   `SDL_OSS_DEFAULT` came out ON for a system with no `/dev/dsp`.

## Notes

* **Why the netbsd audio backend.** Substrate's `<sys/audioio.h>` matches
  NetBSD 10's `audio_info` (`sizeof` 136) with the full
  `AUDIO_GETINFO`/`SETINFO`/`DRAIN` ioctl set, so that backend compiles and
  runs unmodified.  The SDL2 port reached the same backend by rewriting a
  `configure` case; under CMake it is a proper platform branch instead.
* **`HAVE_LINUX_INPUT_H` is pre-seeded false** in `build.sh`.  Substrate ships
  a partial `<linux/input.h>` — enough for the `EVIOCGNAME` probe to pass, not
  enough for the evdev core or the force-feedback ABI to compile.  Input comes
  from X11.
* **`stpcpy` was added to substrate's libc for this port** (`lib/c/src/string.c`,
  declared in `include/string.h`).  `SDL_x11events.c` uses it; it is
  POSIX.1-2008 and was simply missing.

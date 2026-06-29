# SDL 2.x on Substrate

Cross-build of **SDL 2.30.9** as a patch series against the upstream release
(`fetch.sh` downloads + SHA-verifies + extracts; `build.sh` configures + builds
+ stages into `dist-overlay/dist-sdl2/usr/`).

## What's enabled

- **Video:** X11 (substrate's Xlib client stack) + dummy + offscreen.
  wayland / kmsdrm / vulkan / opengl(es) / directfb are off (no driver on target).
- **Audio:** the **NetBSD `audio(4)` backend** drives substrate's Sun/SADA
  `/dev/audio` (`SDL_AUDIO_DRIVER_NETBSD`), plus dummy + disk.  `build.sh`
  routes SDL's `netbsd`-only audio case onto the linux host case (a one-line
  `sed`) so the backend is compiled in, and substrate's `<sys/audioio.h>`
  matches NetBSD 10's `audio_info` (sizeof 136) ioctl ABI byte-for-byte.
- **Threads:** pthreads (libpthread, which gained `pthread_get/setschedparam`).
  **loadso:** dlopen (libdl).
- joystick / haptic / sensor / power / libudev / dbus / hidapi are disabled
  (no corresponding substrate hardware/service).

## Substrate-specific notes

- **Configured as a linux host** (`--host=i386-unknown-linux-gnu`, but
  `CC=i386-unknown-substrate-gcc`).  SDL's bundled libtool has no `substrate`
  host case and would build static-only; substrate is linux-like (ELF,
  pthreads, dlopen, X11), so configuring as linux gives a proper shared library
  while the substrate cross gcc still emits a substrate binary (OSABI 0x40,
  stamped post-build).
- **Linux input core disabled post-configure.**  configure detects substrate's
  `linux/kd.h` + partial `linux/input.h` and turns on the evdev/keyboard core,
  which needs more of the linux UAPI than substrate ships.  Substrate uses X11
  for input, so `build.sh` removes `HAVE_LINUX_INPUT_H` / `SDL_INPUT_LINUXEV` /
  `SDL_INPUT_LINUXKD` from the generated `SDL_config.h`.
- Added `linux/tiocl.h` to substrate's headers (the console `TIOCLINUX`
  sub-commands) — SDL's evdev keyboard referenced it; useful generally.
- **`/dev/audio` device path** (`patches/0001-audiodev-substrate-dev-audio.patch`).
  `SDL_audiodev.c` only maps the default device path to `/dev/audio` when
  `__NETBSD__` / `__OPENBSD__` is defined; the substrate cross gcc defines
  `__substrate__`, so it fell back to the OSS `/dev/dsp` (absent on substrate),
  `SDL_EnumUnixAudioDevices()` found zero devices, and
  `SDL_OpenAudioDevice(NULL, ...)` failed with "No such audio device".  The
  patch adds `__substrate__` to that condition.  Symptom it fixed: PsyMP3
  opened no audio device, no callback ran, and its playback clock stayed at
  0:00.

Build: `./fetch.sh && ./build.sh`.

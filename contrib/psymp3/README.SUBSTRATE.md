# PsyMP3 on Substrate

[PsyMP3](https://github.com/segin/psymp3) is a C++17 SDL2 music player with a
FFT spectrum visualizer, by Kirn Gill II (segin).  This port cross-builds it
for substrate (i386, ELFOSABI_SUBSTRATE).

* Upstream: <https://github.com/segin/psymp3>
* Pinned tag: `1.99.16-RELEASE` (commit `fa24c45a56cc5f8e6497899e6aac6559e7ebe8e5`)
* License: ISC

## Build

```sh
./fetch.sh     # clone + checkout the pinned tag, apply patches/series
./build.sh     # autogen, configure, make, install + OSABI-stamp
```

`build.sh` stages the binary into
`dist-overlay/dist-psymp3/usr/bin/psymp3` and OSABI-stamps it (byte 7 = 0x40).
The UI font and window icon go alongside it as
`usr/share/psymp3/data/{vera.ttf,psymp3.rgba}` — `PSYMP3_DATADIR` is compiled
in as `/usr/share/psymp3/data` (both `src/Makefile.am` and `res/Makefile.am`
override `datadir`), so the player will not find its font if only the binary
is deployed.

### Build host requirements

* substrate stage-1 cross toolchain in `/opt/substrate`
  (`i386-unknown-substrate-{gcc,g++,...}`)
* host `autoconf`, `automake`, **`autoconf-archive`** (provides
  `AX_CXX_COMPILE_STDCXX_17`, referenced by `configure.ac`)
* host `pkg-config`

### How it is configured

PsyMP3 is autotools.  `generate-configure.sh` (== `autogen.sh`) runs
`autoreconf -fiv` on the host.  We then `./configure --host=i386-unknown-linux-gnu`
so libtool/autotools take their well-trodden Linux code path, while `CC`/`CXX`
are the substrate cross compilers — so the emitted objects/binary are substrate
ELF.  `PKG_CONFIG_LIBDIR` points at the cross sysroot's `lib/pkgconfig` so every
dependency resolves from `/opt/substrate/i386-unknown-substrate`.

## Dependencies (all pre-staged in the cross sysroot)

Core (mandatory):

| dep        | version | notes |
|------------|---------|-------|
| SDL2       | 2.30.9  | built with X11 video backend |
| taglib     | 2.0.2   | metadata |
| freetype2  | 26.1.20 | UI text |
| OpenSSL 3  | 3.0.13  | TLS for HTTP/Last.fm |
| libcurl    | 8.7.1   | HTTP streaming |

Codecs (all enabled — every library is staged):

| codec            | provider           |
|------------------|--------------------|
| MP3              | bundled minimp3 (no external dep) |
| FLAC             | native decoder (no libFLAC dep)   |
| Vorbis           | libvorbis 1.3.7 + libogg 1.3.5    |
| Opus             | libopus 1.5.2 + libogg            |
| Speex            | libspeex 1.2.1 + libogg           |
| AAC              | faad2 2.11.1                      |
| G.722            | spandsp 2.0.0                     |
| G.711 A-law/u-law| native (no external dep)          |

## What is disabled (and why)

* `--disable-mpris` — MPRIS is a D-Bus remote-control interface; substrate has
  no system message bus.  (The `src/mpris/` objects still compile — their bodies
  are guarded by `#ifdef HAVE_DBUS` — they just become empty stubs.)
* `--disable-rapidcheck` — RapidCheck property-test library is not ported.
* `--disable-test-harness` — the test programs wrap `SDL_main` and pull in extra
  link deps; not needed for the player itself.
* `--disable-final` — keep the normal multi-translation-unit build rather than
  the KDE3-style single-TU "final" build.

Everything else (all codecs, the full UI/widget stack, HTTP/Last.fm) is enabled.

## Substrate-specific source patches

See `series` / `patches/`.  Each patch is a focused, upstream-shaped change kept
git-apply-able against the pinned tree.  Rationale is in the patch headers.

## Notes

* PsyMP3's platform `#if` ladder keys on `__linux__` / `__FreeBSD__` / `_WIN32`.
  The substrate cross gcc defines `__unix__` + `__substrate__` but **not**
  `__linux__`, so PsyMP3 takes its generic-Unix fallback: the BSD-socket helpers
  are used, `<sys/prctl.h>` / `<SDL_syswm.h>` are **not** included, and
  `System::setThisThreadName()` is a no-op.  This is exactly the behaviour we
  want — no Linux-only syscalls are referenced.
* `build.sh` sets `-fPIE` in `CFLAGS`/`CXXFLAGS` (position-independent *code*),
  but as of upstream `f45a10af` (`Build: Auto-disable PIE on i386 …`) `configure`
  drops the `-pie` *link* flag on i386 — the gate keys on `host_cpu`, added to work
  around a non-PIC *host* TagLib (`ld: relocation R_386_32 in .eh_frame`).  With
  PIC code but a non-PIE link, the substrate `psymp3` is a **non-PIE `ET_EXEC`**
  dynamic binary (`PT_INTERP=/sbin/ld.so`, OSABI 0x40 — verified with `readelf`).
  It still gets `-fstack-protector-strong` (libc provides the symbols) and
  `-pthread`.  The one substrate-specific link need — 64-bit atomic libcalls,
  which neither libgcc nor a (non-existent) libatomic supplies on the i486 target
  — is met by patch `0002` (`src/core/atomic64.c`).
* Upstream `configure` now probes "how to link 64-bit atomics" and answers
  `-march=i586` (i586 has `cmpxchg8b`, so the atomics become inline and no
  library is needed).  `build.sh` passes `-march=i486` *after* that in
  `CFLAGS`/`CXXFLAGS`, and GCC honours the last `-march`, so the build stays on
  i486 and keeps emitting the out-of-line `__atomic_*_8` calls that patch `0002`
  satisfies.  If that ordering ever changes, the binary would silently gain
  `cmpxchg8b` and stop running on a plain i486.

# mpg123 for Substrate

Pinned: **mpg123 1.32.10** (sha256
`87b2c17fe0c979d3ef38eeceff6362b35b28ac8589fbf1854b5be75c9ab6557c`).

Nothing is vendored.  `fetch.sh` downloads the upstream tarball
from `https://www.mpg123.de/download/`, verifies the SHA256, extracts
to `build/mpg123-1.32.10/`, and applies the patch series listed in
`./series` (empty as of writing — mpg123 builds cleanly against the
substrate cross-toolchain without patches).

## Audio backend

Substrate ships a Sun-compat audio framework (NetBSD `/dev/audio`
semantics; see `sys/drivers/audio/` for the Intel HDA, AC'97, and
SB16 drivers).  mpg123 has a built-in **`sun`** output module that
targets exactly this ABI, so the build uses:

    --with-default-audio=sun
    --with-audio=sun

mpg123 will open `/dev/audio` (or whatever the user passes via
`-o /dev/audioN`) and issue the AUDIO_SETINFO / write-samples ioctls
the substrate audio framework already implements.

## What is and isn't built

- **Built:**
  - `mpg123`, `out123`, `mpg123-id3dump`, `mpg123-strip` — substrate
    OS-ABI 32-bit ELF executables, dynamically linked via
    `/sbin/ld.so`.  `mpg123` DT_NEEDEDs `libmpg123.so.0`,
    `libout123.so.0`, `libdl.so.0`, `libsyn123.so.0`, `libm.so.0`,
    `libc.so.0`.
  - `libmpg123.so.0.48.3`, `libout123.so.0.5.1`,
    `libsyn123.so.0.2.3` — plus the `libX.so.0` symlinks and the
    matching `.a` static archives.
  - Output modules as runtime-loadable DSOs (loaded by
    `libout123` via `dlopen` at playback time):
    `mpg123/output_dummy.so`, `mpg123/output_sun.so`.
  - `mpg123.1` / `out123.1` man pages, public headers, pkg-config
    files.
- **Skipped:**
  - Network HTTP streaming (`--disable-network`) — substrate's
    TCP/IP stack isn't wired up yet.
  - Large-file support (`--disable-largefile`) — substrate is 32-bit
    with 32-bit `off_t` on the contrib side.
- **CPU baseline:** `--with-cpu=generic_fpu` (x87 FPU only).
  Substrate's kernel disables SSE/MMX globally.

## Patches applied

`./series` lists, in order:

- `0001-config-sub-recognize-substrate.patch` — teaches the bundled
  `build/config.sub` about the `substrate` OS so that
  `--host=i386-unknown-substrate` doesn't fail at config.sub time.
- `0002-configure-libtool-substrate-shared.patch` — adds `substrate*`
  to the four GNU/ELF case matches in the shipped `configure`
  (lt_cv_deplibs_check_method, lt_prog_compiler_pic, archive_cmds,
  dynamic_linker_features), so libtool flips
  `lt_cv_dynamic_linker_features=yes` / `ld_shlibs=yes` /
  `can_build_shared=yes` for substrate.  Without this configure
  silently falls back to static-only output, even though substrate
  has `lib/dl/libdl.so.0` and ld.so Phase 4e.

## Building

```sh
# 1. Fetch + verify + extract + patch.  One-time per release pin.
./fetch.sh

# 2. Configure + make + stage install.
./build.sh
```

The result lands in `./build/install/usr/local/`:

```
build/install/usr/local/bin/mpg123          (the player)
build/install/usr/local/bin/out123          (raw-output tool)
build/install/usr/local/bin/mpg123-id3dump
build/install/usr/local/bin/mpg123-strip
build/install/usr/local/include/mpg123.h
build/install/usr/local/include/out123.h
build/install/usr/local/include/syn123.h
build/install/usr/local/include/fmt123.h
build/install/usr/local/lib/libmpg123.so -> libmpg123.so.0
build/install/usr/local/lib/libmpg123.so.0 -> libmpg123.so.0.48.3
build/install/usr/local/lib/libmpg123.so.0.48.3
build/install/usr/local/lib/libout123.so.{0,0.5.1}  (+ symlinks)
build/install/usr/local/lib/libsyn123.so.{0,0.2.3}  (+ symlinks)
build/install/usr/local/lib/libmpg123.a
build/install/usr/local/lib/libout123.a
build/install/usr/local/lib/libsyn123.a
build/install/usr/local/lib/mpg123/output_dummy.so
build/install/usr/local/lib/mpg123/output_sun.so
build/install/usr/local/lib/pkgconfig/libmpg123.pc
build/install/usr/local/share/man/man1/mpg123.1
```

To exercise on the target image, copy the binary into `dist/usr/local/bin/`
before re-running `build-rootfs.sh --image`, or add an install step
to `build-rootfs.sh`'s `install_to_dist()` (left as an exercise — the
file paths above are stable).

## Reapplying patches

Drop `*.patch` files into `./patches/` (numbered prefixes recommended
to control ordering — `0001-foo.patch`, `0002-bar.patch`) and list
them one per line in `./series`.  `fetch.sh` reads `./series` and
applies the patches in order; `#`-prefixed and blank lines are
ignored.

## Updating the version

1. Edit `VERSION=` in `fetch.sh`.
2. Run `./fetch.sh` once with the old SHA — it'll fail and print the
   actual computed SHA.
3. Paste the new SHA into `SHA256=` in `fetch.sh`.
4. Re-run `./fetch.sh` — it should now succeed.
5. Re-apply any patches that no longer apply (the series file will
   need editing).

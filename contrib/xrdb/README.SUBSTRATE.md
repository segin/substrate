# xrdb on Substrate

`xrdb` (X resource database utility) reads resource files, runs them through
`cpp`, and loads them into the X server's `RESOURCE_MANAGER` root-window
property.

## Why it's needed

CDE's session start runs `dtsession_res` (dtloadresources), which pipes the
CDE resource files through `/usr/bin/xrdb -merge`.  Without xrdb the session
comes up but logs `/usr/bin/xrdb: inaccessible or not found` and **no**
palette / font / appearance resources load — the desktop "finishes" into a
bare, default-coloured state that looks like it never started.  (It also
needs `/usr/bin/tr`, which build-rootfs symlinks to `/bin/tr`.)

## Build

```sh
./fetch.sh        # xrdb-1.2.2 (SHA-256 verified)
./build.sh        # -> dist-xrdb/usr/bin/xrdb
```

Depends on the staged X client stack (libX11, libXmu and its libXt/ICE/SM
deps).  Configured `--with-cpp="/usr/bin/cpp,/lib/cpp,cpp"`: xrdb invokes a C
preprocessor at runtime to expand the CDE resource files (`#include` /
`#define`), and the stage-2 toolchain installs `cpp` at `/usr/bin/cpp`.

## Verified

After install, `xrdb -query` on a started CDE session returns the loaded
resources (`*ColorPalette`, `*background`, `*foreground`, …) instead of
nothing, and `dtsession_res` no longer errors.

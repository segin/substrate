# ctwm 4.1.0 on Substrate

ctwm — twm extended with virtual screens, workspaces, EWMH hints, and
XPM/title icons.

## Build
```
./fetch.sh        # download + verify + extract + apply series
./build.sh        # CMake cross-build -> dist-ctwm/usr/bin/ctwm
```
Depends on the staged X client chain (xorgproto, libX11, libXext, libXt,
libXmu, libICE, libSM, **libXpm**) plus **libregex**; build those first.

## Substrate notes
- **CMake cross-compile** via `substrate-toolchain.cmake` (sets the cross
  compiler, PIE link, and `CMAKE_FIND_ROOT_PATH` = the merged X mini-sysroot
  so `find_package(X11)` resolves there, while build-host tools — flex,
  bison, perl, sh — are found un-rooted for the parser and generated sources).
- **Feature toggles for missing deps:** `USE_JPEG=OFF` (no libjpeg),
  `USE_XRANDR=OFF` (no libXrandr), `USE_M4=OFF` (no m4 on target; `.ctwmrc`
  is parsed without m4 preprocessing).  XPM stays on (libXpm is ported);
  EWMH and XSMP (libSM/libICE) stay on.
- **regex:** ctwm's `USE_SREGEX` needs POSIX `regcomp`/`regexec`, which on
  substrate live in **libregex**, not libc.  The CMake `check_function_exists`
  probe can't link libregex during cross-config, so the build pre-seeds
  `HAS_REGEX_H`/`HAS_REGEXEC` and links `-lregex` (libregex staged into the
  mini-sysroot).
- **lrand48:** substrate libc has the rand48 `*_r` variants but not the plain
  `lrand48()`; the patch swaps ctwm's two unseeded `lrand48()` calls (icon
  scatter) for `random()` (same range, in libc).
- GCC 16 default-error warnings (`incompatible-pointer-types`, etc.) are
  demoted, as in the glib/pkg-config ports.

## Result
`/usr/bin/ctwm`, a PIE needing libregex + libX11/libXext/libXt/libXmu/
libXpm/libSM/libICE.  Run it against an X server (e.g. Xfbdev) as the
window manager.

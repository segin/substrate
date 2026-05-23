# libxshmfence 1.3.2 — substrate port

Shared-memory fence primitive used by X clients and the X server for
cross-process synchronization (DRI3 sync, the Present extension, the
software-only kdrive paths still rely on it indirectly through
xorg-server's dependency closure).

## Build

    cd contrib/libxshmfence
    ./fetch.sh
    ./build.sh

Stages `dist-libxshmfence/usr/{lib/{libxshmfence.{a,so.1},pkgconfig/
xshmfence.pc},include/X11/xshmfence.h}`.

## Substrate-specific

* `config.sub` patched for substrate (same one-liner as pixman).
* `configure` libtool dispatch sed'd at fetch time so `substrate*` is
  treated like `linux*` for shared-library output.
* The Linux-futex backend is selected (substrate has a futex syscall).
  This required two new compat headers in the substrate userland tree:
  `<linux/futex.h>` (wraps `<sys/futex.h>` + aliases `SYS_futex` to
  substrate's `SYS_FUTEX`) and `<values.h>` (deprecated SVID header
  that maps `MAXINT` -> `INT_MAX` etc).

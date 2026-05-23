# xorg-server 1.16.4 — substrate port (WIP — compiles, doesn't link)

Pinned to 1.16.4: last release that still ships `hw/kdrive/fbdev/`
(Xfbdev) tier-1.  Forward-porting fbdev to 1.20.14's kdrive ABI was
the alternative; staying on 1.16.4 turned out simpler given the
substrate-userland holes were the actual blocker, not the X server
itself.

## Status

* ✓ Fetched + config.sub / libtool patched for substrate.
* ✓ configure passes (every X server flavor except xfbdev disabled).
* ✓ Compiles cleanly through the whole tree with the substrate
  cross-toolchain.  Required ~10 substrate userland additions —
  see the commit log; all landed under include/ and lib/c/src/.
* ✗ Link fails on:
  - `KdOsAddInputDrivers` / `KdOsKeyboardFns` etc — kdrive expects
    a per-OS input bring-up file (hw/kdrive/linux/{keyboard,mouse}.c
    or similar).  Linux's version uses /dev/input/eventN evdev,
    which substrate now also exposes.  Plan: copy hw/kdrive/linux/
    into hw/kdrive/substrate/, patch Linux-specific paths
    (/dev/tty, /dev/console reaches, vt-switch ioctls) over to
    substrate's vtio.h surface.
  - `OsVendorInit` — substrate-specific vendor init.  One-line
    stub: drop a hw/kdrive/src/substrate_os.c with an empty
    OsVendorInit() and add it to the kdrive sources.
  - `FontEncIdentify / FontEncFind / ...` — libXfont references
    libfontenc but the .so doesn't have a DT_NEEDED for it.
    Plan: relink libXfont with `-lfontenc` explicit, or add
    `-lfontenc` to xorg-server's link line.
  - `___tls_get_addr` — pixman shared lib uses ELF TLS, substrate's
    ld.so only supports initial-exec TLS.  Plan: link pixman static
    (its libpixman-1.a is already staged) into xorg-server.

## Substrate userland surface added for this port (commits before/in this branch)

Headers:
- `<linux/fb.h>`, `<linux/types.h>`, `<linux/futex.h>` — compat shims
  pointing at substrate's `<sys/fb.h>` / native types / `<sys/futex.h>`.
- `<netinet/in.h>` — IPv6 multicast macros, in6addr_any /
  in6addr_loopback (declared + defined in libc), IP_/IPV6_
  multicast socket option constants, `struct ip_mreq` / `ipv6_mreq`.
- `<sys/mman.h>` — MAP_ANON alias for MAP_ANONYMOUS.
- `<math.h>` — M_PI / M_E / M_SQRT2 / etc.
- `<limits.h>` — OPEN_MAX = 1024.
- `<string.h>` — declared ffs / ffsl / ffsll / strsignal (libc had
  the impls except strsignal; added).
- `<signal.h>` — SIGIO alias for SIGPOLL.
- `<fcntl.h>` — F_GETOWN/F_SETOWN (no-op accepted), FASYNC, FNDELAY,
  O_NDELAY, O_ASYNC aliases.
- `<sys/time.h>` — implicit `<sys/select.h>` pull-in so fd_set is
  available via the BSD-style include path.
- `<strings.h>` — `#undef` index/rindex before declaration so
  xorgproto's `<X11/Xos.h>` macro definitions don't garble the
  later prototype.
- `<values.h>` — legacy SVID header (MAXINT etc.) — used by
  libxshmfence.

libc:
- `strsignal()` — POSIX 2008, mirrors psignal name table.
- `in6addr_any` / `in6addr_loopback` — RFC 3493 globals.

Kernel (already landed in vt.c / vt branch):
- `KDSETMODE` / `KDGETMODE` / `KDSKBMODE` / `KDGKBMODE` ioctls so
  the X server can claim the VT.

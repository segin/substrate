# xorg-server — substrate port (WIP)

Pinned to 1.20.14 (last autotools-friendly release).  kdrive support
is present but only ships the Xephyr backend; **Xfbdev was removed
upstream in the 1.17 era**, so the substrate plan is to resurrect it
from 1.16.4's `hw/kdrive/fbdev/` and forward-port it onto the 1.20.14
internal APIs.

Status:
- ✓ tarball fetched, configure / libtool patched for substrate
- ✗ resurrection-of-Xfbdev not yet implemented
- ✗ build.sh exists but won't produce a usable Xfbdev binary yet

## Forward-port plan

1.  Drop `hw/kdrive/fbdev/{fbdev.c,fbdev.h,fbinit.c,Makefile.am}`
    from 1.16.4 into 1.20.14's `hw/kdrive/`.
2.  Adapt to 1.20's kdrive ABI:
    - `KdScreenInfo` fields renamed.
    - Input model changed (KdAddDevices vs new keyboard/mouse setup).
    - `EnableScreen` / `DisableScreen` signatures.
3.  Substrate-specific:
    - replace Linux `<linux/fb.h>` ioctls (FBIOGET_VSCREENINFO,
      FBIOGET_FSCREENINFO, FBIOPAN_DISPLAY) with substrate's
      framebuffer ioctls (FBIOGET_*, see sys/include/sys/fb.h).
    - input via `/dev/input/event0` evdev path that substrate
      now supplies — kdrive's `linux` input backend should be
      adaptable.

## Open holes surfaced during the 1.16.4 D-path probe
(documenting so the 1.20.14 + Xfbdev attempt doesn't re-discover):

- substrate libc lacks: `in6addr_any`, `IP_MULTICAST_TTL`,
  `IPV6_MULTICAST_HOPS`, `IN_MULTICAST` macro, `IN6_IS_ADDR_MULTICAST`
  macro, `MAP_ANON` (substrate uses `MAP_ANONYMOUS`).  XDMCP and
  Xtrans want these.  Either stub them in substrate's headers
  (we already added a placeholder for IN6 macros — see
  contrib/libX11 README) or `--disable-xdmcp` if the server build
  permits.
- IP_MULTICAST / IPV6 multicast functionality won't actually work
  even with the constants stubbed — substrate's kernel networking
  doesn't have multicast.  XDMCP discovery would always fail.
  Static `-display :0` startup should still work.


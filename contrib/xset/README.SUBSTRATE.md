# xset on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/app/xset
Pinned:    1.2.5
Tarball:   `https://www.x.org/releases/individual/app/xset-1.2.5.tar.xz`
SHA-256:   `9f692d55635b3862cd63633b1222a87680ec283c7a8e8ed6dd698a3147f75e2f`

## Why

`xset` is the X user-preferences utility: keyboard auto-repeat and click,
bell pitch/volume, pointer acceleration, screen-saver and DPMS timeouts,
the font path (`xset fp`), and LED state.

## Build

    ./fetch.sh && ./build.sh       # -> dist-overlay/dist-xset/usr/bin/xset

Depends on `xorgproto`, `libX11`, `libXext`, and `libXmu` (the `xmuu`
pkg-config module = libXmuu).  Configured `--without-fontcache`:
substrate has neither the removed XFontCache extension nor the legacy
XFree86-Misc extension (xf86misc stays off by default), so those two
optional `xset` features are compiled out; everything else is present.
The only substrate patch teaches `config.sub` the target triple.

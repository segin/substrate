# xsetroot on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/app/xsetroot
Pinned:    1.1.3
Tarball:   `https://www.x.org/releases/individual/app/xsetroot-1.1.3.tar.xz`
SHA-256:   `6081b45a9eb4426e045d259d1e144b32417fb635e5b96aa90647365ac96638d1`

## Why

`xsetroot` sets X root-window attributes: the background (a solid
colour, a bitmap, or the default gray weave), the pointer cursor
(`-cursor` from two bitmaps, or `-cursor_name` from the cursor theme),
and `-def` to reset to defaults.

## Build

    ./fetch.sh && ./build.sh       # -> dist-overlay/dist-xsetroot/usr/bin/xsetroot

Depends on `xorgproto`, `libX11`, `libXmu` (`xmuu`), `libXrender`,
`libXfixes`, and two ports added for it: **xbitmaps** (the `<X11/bitmaps>`
images + `xbitmaps.pc`, installed under `share/pkgconfig`) and
**libXcursor** (the `xcursor` module, which xsetroot 1.1.x hard-requires
for `-cursor_name`).  The only substrate patch teaches `config.sub` the
target triple.

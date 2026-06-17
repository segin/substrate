# libXcursor on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/libxcursor
Pinned:    1.2.3
Tarball:   `https://www.x.org/releases/individual/lib/libXcursor-1.2.3.tar.xz`
SHA-256:   `fde9402dd4cfe79da71e2d96bb980afc5e6ff4f8a7d74c159e1966afb2b2c2c0`

## Why

The Xcursor library: loads themed and ARGB (full-colour, animated) mouse
cursors and provides the `xcursor` pkg-config module.  Added as a
prerequisite for `xsetroot` (`-cursor_name`); also useful to any client
that wants cursor-theme support.

## Build

    ./fetch.sh && ./build.sh       # -> dist-overlay/dist-libXcursor/usr/lib/libXcursor.so.1

Depends on `xorgproto`, `libX11`, `libXrender`, and `libXfixes` (all
already ported).  Standard autotools X-lib port: `config.sub` learns the
target triple, the libtool `configure` host_os dispatch treats
`substrate*` like `linux*` for shared libraries, and
`xorg_cv_malloc0_returns_null=no` is preseeded for the cross build.
Produces shared + static libs; the `.so` is OSABI-stamped to
ELFOSABI_SUBSTRATE.

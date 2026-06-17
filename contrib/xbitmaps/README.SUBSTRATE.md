# xbitmaps on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/data/bitmaps
Pinned:    1.1.3
Tarball:   `https://www.x.org/releases/individual/data/xbitmaps-1.1.3.tar.xz`
SHA-256:   `ad6cad54887832a17d86c2ccfc5e52a1dfab090f8307b152c78b0e1529cd0f7a`

## Why

A NOCODE (header-only) data package: the common X11 bitmap images
(`gray`, `root_weave`, the `xlogo*`, ...) installed under
`<X11/bitmaps/>`, plus the `xbitmaps.pc` pkg-config file.  Required by
`xsetroot` (its default-background gray weave) and various other X
clients that `#include <X11/bitmaps/...>`.

## Build

    ./fetch.sh && ./build.sh       # -> dist-overlay/dist-xbitmaps/usr/...

No compilation; `make install` stages the headers and
`usr/share/pkgconfig/xbitmaps.pc`.  The only substrate patch teaches
`config.sub` the target triple (configure still validates the host).

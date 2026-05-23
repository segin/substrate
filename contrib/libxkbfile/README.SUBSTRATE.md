# libxkbfile 1.1.3 — substrate port

X keyboard extension (XKB) file-format library — parses keymap files,
used by xorg-server's XKB extension and by xkbcomp.

## Build

    cd contrib/libxkbfile
    ./fetch.sh
    ./build.sh

Stages dist-libxkbfile/usr/{lib,include/X11/extensions,lib/pkgconfig}.

## Substrate-specific

* Same `config.sub` + libtool dispatch tweaks as pixman / libxshmfence.
* Depends on `dist-xorgproto` and `dist-libX11`.
* Required a `<strings.h>` fix in the substrate userland tree: xorgproto's
  `<X11/Xos.h>` `#define`s `index` and `rindex` as function-like macros
  pointing to `strchr`/`strrchr`, so any later `#include <strings.h>`
  expanded the macros INSIDE the `char *index(const char *s, int c);`
  declaration and produced parse-error garbage.  `<strings.h>` now
  `#undef`s the macros before declaring.

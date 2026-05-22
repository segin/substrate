# xtrans on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/libxtrans
Pinned:    1.6.0
Tarball:   `https://www.x.org/releases/individual/lib/xtrans-1.6.0.tar.xz`
SHA-256:   `faafea166bf2451a173d9d593352940ec6404145c5d1da5c213423ce4d359e92`

## Why

`xtrans` is the X11 network-transport abstraction.  It is shipped
as header + `.c` files that the *consumer* `#include`s and compiles
itself: `libX11` builds `Xtranssock.c` / `Xtranslcl.c` straight
into `libX11.so`.  `libX11` will not configure without `xtrans.pc`.

## Scope

- Transport sources  → `/usr/include/X11/Xtrans/*.{h,c}`
- autoconf macro     → `/usr/share/aclocal/xtrans.m4`
- pkg-config metadata → `/usr/lib/pkgconfig/xtrans.pc`

No compiled objects are produced.

## Substrate patches

- `0001-config-sub-substrate.patch` — teach `config.sub` the
  `substrate*` OS name.

The transport `.c` files carry their own `#ifdef`s for abstract
sockets / peer-credential APIs, but the actual transport selection
(`UNIXCONN` / `TCPCONN` / `LOCALCONN` and `HAVE_ABSTRACT_SOCKETS`)
is decided by the *consumer's* `configure` — see
`contrib/libX11`.  Substrate's AF_UNIX + TCP socket layer is the
runtime backing; substrate has no abstract-socket namespace, so
the filesystem-socket path is used.

## Layout

    contrib/xtrans/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

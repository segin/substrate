# libXext on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/
Pinned:    1.3.7
Tarball:   `https://www.x.org/releases/individual/lib/libXext-1.3.7.tar.xz`
SHA-256:   `6c643c7035cdacf67afd68f25d01b90ef889d546c9fcd7c0adf7c2cf91e3a32d`

## Why

libXext is the X11 protocol-extensions client library (shape, shm, sync, ...) — part of the X library stack required to build
`contrib/xterm`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix so `--enable-shared` emits real `.so` files.

## Layout

    contrib/libXext/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

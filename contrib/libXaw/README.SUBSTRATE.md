# libXaw on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/
Pinned:    1.0.16
Tarball:   `https://www.x.org/releases/individual/lib/libXaw-1.0.16.tar.xz`
SHA-256:   `731d572b54c708f81e197a6afa8016918e2e06dfd3025e066ca642a5b8c39c8f`

## Why

libXaw is the Athena widget set — part of the X library stack required to build
`contrib/xterm`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix so `--enable-shared` emits real `.so` files.

## Layout

    contrib/libXaw/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

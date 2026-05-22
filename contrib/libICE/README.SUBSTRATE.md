# libICE on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/
Pinned:    1.1.2
Tarball:   `https://www.x.org/releases/individual/lib/libICE-1.1.2.tar.xz`
SHA-256:   `974e4ed414225eb3c716985df9709f4da8d22a67a2890066bc6dfc89ad298625`

## Why

libICE is the Inter-Client Exchange protocol library — part of the X library stack required to build
`contrib/xterm`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix so `--enable-shared` emits real `.so` files.

## Layout

    contrib/libICE/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

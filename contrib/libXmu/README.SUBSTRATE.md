# libXmu on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/
Pinned:    1.3.1
Tarball:   `https://www.x.org/releases/individual/lib/libXmu-1.3.1.tar.xz`
SHA-256:   `81a99e94c4501e81c427cbaa4a11748b584933e94b7a156830c3621256857bc4`

## Why

libXmu is the X miscellaneous-utilities library — part of the X library stack required to build
`contrib/xterm`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix so `--enable-shared` emits real `.so` files.

## Layout

    contrib/libXmu/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

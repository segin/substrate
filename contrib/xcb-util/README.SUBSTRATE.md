# xcb-util on Substrate

xcb-util 0.4.1, cross-built for `i386-unknown-substrate`.

Upstream tarball: `https://xcb.freedesktop.org/dist/xcb-util-0.4.1.tar.gz`
(SHA256 verified by `fetch.sh`).

## Building

```sh
./fetch.sh      # download + verify + extract
./build.sh      # cross-configure, make, stage into dist-overlay/dist-xcb-util
```

`build.sh` stages into `${SUBSTRATE_TOP}/dist-overlay/dist-xcb-util/usr`, which
`build-rootfs.sh` overlays onto the image.

## Substrate notes

No patch series: the only fixup the tree needs is a `config.sub` that knows
the `i386-unknown-substrate` triple, and `build.sh` applies that through
`substrate_config_sub_fix` from `contrib/substrate-autotools.sh`.

Dependency include/library flags are assembled from the staged
`dist-overlay/dist-*` trees, so the X libraries this links against must be
built and staged first.

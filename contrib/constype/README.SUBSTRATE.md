# constype on Substrate

constype 1.0.6, cross-built for `i386-unknown-substrate`.

Upstream tarball: `https://www.x.org/archive/individual/app/constype-1.0.6.tar.gz`
(SHA256 verified by `fetch.sh`).

## Building

```sh
./fetch.sh      # download + verify + extract
./build.sh      # cross-configure, make, stage into dist-overlay/dist-constype
```

`build.sh` stages into `${SUBSTRATE_TOP}/dist-overlay/dist-constype/usr`, which
`build-rootfs.sh` overlays onto the image.

## Substrate notes

No patch series: the only fixup the tree needs is a `config.sub` that knows
the `i386-unknown-substrate` triple, and `build.sh` applies that through
`substrate_config_sub_fix` from `contrib/substrate-autotools.sh`.

Dependency include/library flags are assembled from the staged
`dist-overlay/dist-*` trees, so the X libraries this links against must be
built and staged first.

## Builds, but stages nothing

`constype` reports the Sun console type and upstream only implements it for
SunOS.  On every other platform its `Makefile` prints

    constype not supported on this platform

and installs no binary, so `dist-overlay/dist-constype` is empty by design.
The port is kept so the module is accounted for rather than silently absent.

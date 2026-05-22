# XCB protocol descriptions (xcb-proto) on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/proto/xcbproto
Pinned:    1.17.0
Tarball:   `https://xcb.freedesktop.org/dist/xcb-proto-1.17.0.tar.xz`
SHA-256:   `2c1bacd2110f4799f74de6ebb714b94cf6f80fb112316b1219480fd22562148c`

## Why

`xcb-proto` is a **build-time** dependency of `libxcb`.  It ships:

- the X protocol descriptions as XML (`/usr/share/xcb/*.xml`)
- the `xcbgen` Python package, which `libxcb`'s build imports to
  turn those XML descriptions into C source (`xcb_*.c` / `.h`)
- `xcb-proto.pc`, which `libxcb`'s `configure` reads for the
  version check and to locate the XML + `xcbgen` directories

Nothing here is compiled.  `contrib/libxcb/build.sh` points
`PYTHONPATH` and the XML search path at this port's staging tree.

## Scope

- Protocol XML        → `/usr/share/xcb/`
- `xcbgen` package    → `/usr/lib/python*/site-packages/xcbgen/`
- pkg-config metadata → `/usr/lib/pkgconfig/xcb-proto.pc`

## Substrate patches

None.  `xcb-proto` has no compiled code and its `configure` does
not canonicalise the host triple, so `--host=i386-unknown-substrate`
is accepted as-is.

## Layout

    contrib/xcb-proto/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

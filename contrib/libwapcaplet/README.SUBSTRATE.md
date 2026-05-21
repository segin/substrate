# libwapcaplet

NetSurf string-interning library — interned, reference-counted,
hash-indexed strings used across the NetSurf HTML/CSS stack.

Upstream: <https://www.netsurf-browser.org/projects/libwapcaplet/>
Pinned version: **libwapcaplet-0.4.3**
License: MIT (see `build/libwapcaplet-<ver>/COPYING`).
Substrate vendoring: tarball + NetSurf buildsystem + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-libwapcaplet/usr/` with `lib/libwapcaplet.a`, the
`include/libwapcaplet/` headers, and the pkg-config file.

## Notes

Built with the NetSurf shared buildsystem (see
`contrib/libparserutils` for details).  Static library; no source
patches required.

Part of the NetSurf DOM/CSS stack pulled in by `contrib/elinks`.

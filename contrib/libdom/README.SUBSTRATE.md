# libdom

NetSurf DOM library — an implementation of the W3C Document Object
Model, the document tree that scripting engines manipulate.

Upstream: <https://www.netsurf-browser.org/projects/libdom/>
Pinned version: **libdom-0.4.2**
License: MIT (see `build/libdom-<ver>/COPYING`).
Substrate vendoring: tarball + NetSurf buildsystem + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-libdom/usr/` with `lib/libdom.a`, headers and the
pkg-config file.

## Notes

Depends on **libparserutils**, **libwapcaplet** and **libhubbub** —
all must be staged in the cross-toolchain sysroot first.

Built with the NetSurf shared buildsystem (see
`contrib/libparserutils`).  `WITH_EXPAT_BINDING=no` — substrate has
no expat, and `contrib/elinks` drives libdom through the hubbub
(HTML) binding rather than the XML one.  Static library; no source
patches required.

Required by `contrib/elinks` whenever its ECMAScript backend is
enabled — libdom is the DOM that QuickJS scripts act on.

# libparserutils

NetSurf parser-utils library — character-set conversion, growable
buffers, stacks, vectors and input streams used across the NetSurf
HTML/CSS component stack.

Upstream: <https://www.netsurf-browser.org/projects/libparserutils/>
Pinned version: **libparserutils-0.2.5**
License: MIT (see `build/libparserutils-<ver>/COPYING`).
Substrate vendoring: tarball + NetSurf buildsystem + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-libparserutils/usr/` with `lib/libparserutils.a`,
the `include/parserutils/` headers, and the
`lib/pkgconfig/libparserutils.pc` file.

## Notes

NetSurf components build with the shared **buildsystem** makefiles
(`buildsystem-1.10`, fetched alongside the library).  The cross
compiler is passed as `CC`; the buildsystem derives `HOST` from
`CC -dumpmachine` and switches to cross mode automatically when
`BUILD != HOST`.  Built as a static library — only `contrib/elinks`
(via libcss / libdom) consumes it.  No source patches required.

Part of the NetSurf DOM/CSS stack pulled in by `contrib/elinks`:
libparserutils, libwapcaplet, libhubbub, libcss, libdom.

# libcss

NetSurf CSS engine — a CSS parser and selection engine.

Upstream: <https://www.netsurf-browser.org/projects/libcss/>
Pinned version: **libcss-0.9.2**
License: MIT (see `build/libcss-<ver>/COPYING`).
Substrate vendoring: tarball + NetSurf buildsystem + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-libcss/usr/` with `lib/libcss.a`, headers and the
pkg-config file.

## Notes

Depends on **libparserutils** and **libwapcaplet** — both must be
staged in the cross-toolchain sysroot first.

Built with the NetSurf shared buildsystem (see
`contrib/libparserutils`).  `gperf` (host) generates the property
parser tables.  Static library; no source patches required.

Required by `contrib/elinks` whenever its ECMAScript backend is
enabled (elinks drives page styling through libcss).

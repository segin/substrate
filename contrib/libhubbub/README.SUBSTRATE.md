# libhubbub

NetSurf HTML5 parsing library — a permissive, streaming HTML5
tokeniser and tree constructor.

Upstream: <https://www.netsurf-browser.org/projects/libhubbub/>
Pinned version: **libhubbub-0.3.8**
License: MIT (see `build/libhubbub-<ver>/COPYING`).
Substrate vendoring: tarball + NetSurf buildsystem + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-libhubbub/usr/` with `lib/libhubbub.a`, headers and
the pkg-config file.

## Notes

Depends on **libparserutils** and **libwapcaplet** — both must be
staged in the cross-toolchain sysroot first (their pkg-config `.pc`
files, with the prefix rewritten to the sysroot path).

Built with the NetSurf shared buildsystem (see
`contrib/libparserutils`).  Code generation (`perl` for the entity
table, `gperf` for the element-type hash) runs on the build host.
Static library; no source patches required.

Part of the NetSurf DOM/CSS stack pulled in by `contrib/elinks`.

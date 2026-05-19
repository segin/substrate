# zlib

Mark Adler / Jean-loup Gailly's `zlib` compression library
(<https://zlib.net/>).

Upstream: <https://github.com/madler/zlib>
Pinned version: **v1.3.1**
License: zlib license (see `build/zlib-<ver>/LICENSE`).
Substrate vendoring: tarball + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-zlib/usr/{lib,include}` with `libz.a`, `libz.so.1`,
`libz.so`, plus `zlib.h` and `zconf.h`.  Pulled in by `contrib/mandoc`
for `.gz`-compressed man-page reading, and by future `contrib/qman`
for the same.

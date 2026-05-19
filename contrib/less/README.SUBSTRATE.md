# less

Mark Nudelman's `less` pager (<https://www.greenwoodsoftware.com/less/>).

Upstream: <https://www.greenwoodsoftware.com/less/>
Pinned version: **less-692**
License: The Less License (BSD-style, see `build/less-<ver>/LICENSE`).
Substrate vendoring: tarball + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-less/usr/bin/{less,lessecho,lesskey}` and the
matching man pages.

## Substrate-specific overrides

- `--with-regex=posix` — substrate has its own POSIX `libregex`,
  but no `libpcre`/`libpcre2`.  Force the POSIX regex backend.
- `ac_cv_lib_tinfo*=no` — substrate ncurses doesn't split its
  termcap routines into a separate `libtinfo`; everything lives
  in `libncurses`.  Skip the probe so `less` doesn't try to
  link `-ltinfo`.

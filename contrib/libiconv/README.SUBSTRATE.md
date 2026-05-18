# GNU libiconv on substrate

Upstream:  https://www.gnu.org/software/libiconv/
Pinned:    1.17    (released 2022-05-15)
Tarball:   `https://ftp.gnu.org/gnu/libiconv/libiconv-1.17.tar.gz`
SHA-256:   `8f74213b56238c85a50a5329f77e06198771e70dd9a739779f4c02f65d971313`

## Why

Substrate's in-tree libc has `iconv(3)` only as an ENOSYS stub.  A
full libiconv gives us the standard `iconv` / `iconv_open` /
`iconv_close` API with all the legacy encodings (KOI8-R, EUC-JP,
GBK, BIG5, Mac OS Roman, every Windows codepage, …) plus an
`iconv(1)` driver for shell use.

Required by larger ports — anything that does text I/O across
locales eventually wants it (GTK / GNOME stack, the Perl text
modules, GNU coreutils' `unicode` build options, modern git's
`pretty=%h` rendering).

## Scope

- `libiconv.so` shared library → `/usr/lib/libiconv.so.{2,2.6.1}`
- `iconv.h` header           → `/usr/include/iconv.h`
- `iconv(1)` binary          → `/usr/bin/iconv`

## Layout

    contrib/libiconv/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

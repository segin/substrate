# luit on substrate

Upstream:  https://invisible-island.net/luit/
Pinned:    20250912
Tarball:   `https://invisible-island.net/archives/luit/luit-20250912.tgz`
SHA-256:   `46958060e66f35bcb8a51ba22da1c13d726d28a86c1cf520511bcf7914bef39e`

## Why

`luit` is the Unicode/locale ISO-2022 filter from Thomas Dickey's
tree — the same source `xterm` is built from.  It sits between a
terminal program and an application whose encoding does not match
the terminal's, converting bytes in both directions.

The canonical use is `xterm -lc`: xterm in a UTF-8 locale spawns
`luit` as a child to run legacy-encoded programs (KOI8-R, EUC-JP,
ISO-8859-5, …) without garbling the display.  xterm needs `luit`
in `$PATH` for `-lc` to work at all.

## Scope

- `/usr/bin/luit`
- man page → `/usr/share/man/man1/luit.1`

## Depends on

Two libraries — far fewer than xterm:

- `contrib/libfontenc` — charset bitmap lookup tables (`fonts.dir`
  entries reference the same encoding names luit accepts).
- `contrib/libiconv` — the actual byte-level conversion engine.

Notably **no X library chain**: luit is a pty filter and does no
Xlib calls.  Its only `xorg`-flavour dependency is libfontenc's
encoding registry.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.

## Build notes

- `--disable-iconv-cache` skips the build-time iconv-converter
  enumeration that luit otherwise embeds; that probe would run a
  host-built helper that doesn't see substrate's libiconv anyway.
- No special `ac_cv_func_*` overrides needed — luit's autoconf
  probes only touch portable POSIX surface that substrate's libc
  already provides.

## Notes

luit is a pure userland tool; it does not need a running X server.
It's invoked as a transparent child by xterm or directly from the
shell when piping legacy-encoded text through a UTF-8 terminal.

## Layout

    contrib/luit/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

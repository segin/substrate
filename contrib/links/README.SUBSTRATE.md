# links

Twibright Links — the Mikuláš Patočka console/graphics web browser
(<http://links.twibright.com/>).

Upstream: <http://links.twibright.com/>
Pinned version: **links-2.30**
License: GPL-2.0-or-later (see `build/links-<ver>/COPYING`).
Substrate vendoring: tarball + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-links/usr/bin/links` and `usr/share/man/man1/links.1`.

## Configuration

Console-only build — graphics mode is left disabled, so no X /
framebuffer / svgalib / image-codec libraries are required.

- HTTPS: the substrate **OpenSSL** port (`--with-ssl`,
  `--disable-ssl-pkgconfig`).
- Content compression: **zlib** + **bzip2** (both auto-detected
  from the cross-toolchain sysroot).
- `--without-libevent` — substrate has no libevent; links falls
  back to its built-in `select()` event loop.
- `--without-x` — no X Window System.

## Substrate-specific overrides

- **`0001-config-sub-substrate.patch`** — links-2.30 ships an
  autoconf-2.13 `config.sub` whose OS list uses `-<name>*`
  patterns; add `-substrate*` so `i386-unknown-substrate` triples
  canonicalize instead of being rejected as an unknown system.

The autoconf-2.13 `configure` does not accept the modern
`./configure VAR=value` form and does not derive the cross
compiler from `--host`, so `build.sh` exports `CC`, `CFLAGS` and
`LDFLAGS` in the environment instead.

## Notes

Twibright Links has no JavaScript engine and no scripting hooks;
the `contrib/quickjs` port is a separate, standalone JS engine and
is not wired into this browser.

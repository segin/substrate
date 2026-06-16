# GNU sed — Substrate port

GNU sed 4.9 cross-built for substrate via the stage-1 GNU toolchain.

## Build chain
- `fetch.sh` — download + SHA-verify + extract sed-4.9.tar.xz, apply
  the substrate patch series from `series`.
- `build.sh` — `./configure --host=i386-unknown-substrate ...` +
  `make` + stage into `${SUBSTRATE_TOP}/dist-overlay/dist-sed/usr/`.

## Substrate-side use
On the image, `/usr/bin/sed` is GNU sed.  Substrate's hand-rolled
`bin/sed` keeps building (disabled at the SUBDIRS level if it
shadows) — the GNU version covers every BRE/ERE extension
autoconf scripts rely on (`\?`, `\+`, `\b`, `\<`, `\>`, `-E`,
`-i`, `-r`, …).

## Patch series
See `series`.  Patches in `patches/` follow the same one-line
`config.sub` registration the other GNU ports carry; additional
patches land here as build-time issues surface.

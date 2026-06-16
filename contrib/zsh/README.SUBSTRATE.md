# zsh — Substrate port

zsh 5.9 cross-built for substrate via the stage-1 GNU toolchain.

## Build chain
- `fetch.sh` — download + extract zsh-5.9.tar.xz, apply the
  substrate patch series from `series`.
- `build.sh` — `./configure --host=i386-unknown-substrate ...`
  + `make` + stage into `${SUBSTRATE_TOP}/dist-overlay/dist-zsh/usr/`.

## Substrate-side use
On the image, `/bin/sh` points at `/usr/bin/zsh` (zsh detects the
`sh` invocation name and runs in POSIX-compatible mode).  zsh's
shell-feature coverage clears autoconf-generated configure
scripts' shell-sniffing, which substrate's hand-rolled sh
doesn't (no <<- in older versions, incomplete function semantics
for autoconf's probes, etc).

## Patch series
See `series`.  Patches in `patches/` should match what's
applied by `fetch.sh` immediately after extraction.

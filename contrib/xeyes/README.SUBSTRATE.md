# xeyes (substrate port)

The classic X demo: a pair of eyes that follow the pointer.  A handy
end-to-end smoke test of the ported Xlib + Xt + Xext (SHAPE) + Xmu
stack and of pointer-motion delivery.

- **Upstream:** xeyes 1.1.2 (`https://www.x.org/releases/individual/app/`)
- **Layout:** patch series against the upstream tarball; nothing vendored.
  `fetch.sh` downloads + SHA-verifies + extracts + applies `patches/`.
- **Build:** `./fetch.sh && ./build.sh` → stages `/usr/bin/xeyes` into
  `dist-xeyes/`.
- **Dependencies (staged first):** xorgproto, libxcb, libXau, xtrans,
  libX11, libXext, libXmu, libICE, libSM, libXt.

## Substrate specifics

- `0001-config-sub-substrate.patch` teaches the bundled (older) `config.sub`
  the `-substrate` OS name so `--host=i386-unknown-substrate` validates.
- Built `--without-xrender`: libXrender is not ported, so xeyes uses the
  SHAPE-extension rendering path (libXext) instead of anti-aliased Xrender.
- Needs a running X server (e.g. Xfbdev) and `DISPLAY` set.

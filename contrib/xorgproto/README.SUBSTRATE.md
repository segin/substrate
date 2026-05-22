# X.Org protocol headers (xorgproto) on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/proto/xorgproto
Pinned:    2024.1
Tarball:   `https://www.x.org/releases/individual/proto/xorgproto-2024.1.tar.xz`
SHA-256:   `372225fd40815b8423547f5d890c5debc72e88b91088fbfb13158c20495ccb59`

## Why

`xorgproto` is the bottom of the X11 client stack.  It is a
header-only package: the core X protocol definitions (`X.h`,
`Xproto.h`, `keysymdef.h`), the extension protocol headers under
`X11/extensions/`, and the GLX headers under `GL/`.  Every other
X11 port depends on it:

    xorgproto  ──>  libXau  ──>  libxcb  ──>  libX11
            └──────────────────┴──>  (xtrans is header-only too)

## Scope

- Core + extension protocol headers → `/usr/include/X11/...`, `/usr/include/GL/...`
- pkg-config metadata (`xproto.pc`, `xextproto.pc`, `xcb-proto`'s
  siblings, …) → `/usr/lib/pkgconfig/*.pc`

No compiled objects are produced.

## Substrate patches

- `0001-config-sub-substrate.patch` — teach the bundled
  `config.sub` the `substrate*` OS name so `--host=i386-unknown-substrate`
  passes configure's host-triple validation.

## Notes

xorgproto installs its `.pc` files to `$(datadir)/pkgconfig`
(`/usr/share/pkgconfig`).  `build.sh` relocates them into
`/usr/lib/pkgconfig` after install so the repo-root `build.sh`
`sync_to_sysroot` step (which only mirrors `usr/lib` + `usr/include`)
carries them into the cross sysroot for the downstream X11 ports.

## Layout

    contrib/xorgproto/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

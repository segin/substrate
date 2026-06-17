# xprop on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/app/xprop
Pinned:    1.2.8
Tarball:   `https://www.x.org/releases/individual/app/xprop-1.2.8.tar.xz`
SHA-256:   `d689e2adb7ef7b439f6469b51cda8a7daefc83243854c2a3b8f84d0f029d67ee`

## Why

`xprop` displays (and can set/remove) window and font properties on an X
server — `xprop -root` dumps the root-window properties.  Session
startup scripts use it to probe the root window; TDE's `starttde` calls
`xprop` to detect an existing session, and without it logs
`command not found: xprop`.

## Build

    ./fetch.sh && ./build.sh       # -> dist-overlay/dist-xprop/usr/bin/xprop

Depends only on `xorgproto` and `libX11`.  Standard autotools X-app
port; the single substrate patch teaches `config.sub` the
`i386-unknown-substrate` triple.  The executable is ELFOSABI 0, which
the kernel ELF loader runs as native via the `/sbin/ld.so` interp.

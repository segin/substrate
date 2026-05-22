# xauth on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/app/xauth
Pinned:    1.1.5
Tarball:   `https://www.x.org/releases/individual/app/xauth-1.1.5.tar.xz`
SHA-256:   `a4000e2f441facebf569026bedecc23ba262cc6927be52070abe0002625cfbe0`

## Why

`xauth` edits the X authority file (`~/.Xauthority`) — the
`MIT-MAGIC-COOKIE-1` records an X client presents to an X server.
Without an entry for the target display, a client connecting to an
auth-protected server fails with:

    Authorization required, but no authorization protocol specified
    ... Xt error: Can't open display

`xauth` is what lets `xterm` (and every other X client) authenticate.
SSH X11 forwarding also drives `xauth` to install the per-session
cookie on the remote side.

## Scope

- `/usr/bin/xauth`
- man page → `/usr/share/man/man1/xauth.1`

## Depends on

`contrib/{xorgproto,libxcb,libXau,xtrans,libX11,libXext,libXmu}` —
links `libX11`, `libXau`, `libXext` and `libXmuu`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.

(No libtool patch: xauth builds only an executable, so its
`configure` has no shared-library host_os dispatch.)

## Layout

    contrib/xauth/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored

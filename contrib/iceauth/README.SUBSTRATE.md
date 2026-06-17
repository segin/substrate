# iceauth on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/app/iceauth
Pinned:    1.0.10
Tarball:   `https://www.x.org/releases/individual/app/iceauth-1.0.10.tar.xz`
SHA-256:   `3deefb7da26af9dc799b5628d929d91c9af68c78575639944db3b955f29aa029`

## Why

`iceauth` edits the ICE authority file (`~/.ICEauthority`) — the
authentication records ICE (Inter-Client Exchange) clients present to
connect to an ICE listener.  It is the ICE analogue of `xauth`, and the
tool an X session manager's framework drives to authorize ToolTalk /
session-management connections.  TDE's build references it as
`ICEAUTH_PATH`.

## Build

    ./fetch.sh && ./build.sh      # -> dist-overlay/dist-iceauth/usr/bin/iceauth

Depends on the `xorgproto` and `libICE` ports.  Standard autotools X-app
port: the only substrate patch teaches `config.sub` the
`i386-unknown-substrate` host triple.  The executable is left at
ELFOSABI 0 (SYSV) like every other autotools app — the kernel's ELF
loader treats OSABI 0 with interp `/sbin/ld.so` as a native binary.

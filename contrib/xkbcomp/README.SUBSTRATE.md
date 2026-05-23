# xkbcomp 1.4.7 — substrate port

X keyboard map compiler.  Consumes XKB layout descriptions from
xkeyboard-config and produces compiled keymaps the X server loads at
runtime.  Required by xorg-server.

## Build

    cd contrib/xkbcomp
    ./fetch.sh
    ./build.sh

Stages dist-xkbcomp/usr/bin/xkbcomp.

## Substrate-specific

* Same `config.sub` + libtool dispatch tweaks as the other X ports.
* Depends on `dist-xorgproto`, `dist-libX11`, `dist-libxkbfile`.

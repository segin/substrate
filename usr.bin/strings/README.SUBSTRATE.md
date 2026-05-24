# `strings` — shipped by GNU binutils

substrate's `/usr/bin/strings` comes from the **GNU binutils**
stage-2 build (`contrib/binutils/`).  `build-rootfs.sh`'s
`install_to_dist()` overlays `dist-toolchain/usr/bin/strings`
into the rootfs at `/usr/bin/strings`.

This directory exists only so the recursive
`make -C usr.bin` SUBDIRS iteration doesn't break.  The Makefile
here is intentionally empty — no per-utility build runs.

If you want substrate-native (libelfobj-based) `strings`, see git
history for the now-retired `TASKLIST_STRINGS.md`.
